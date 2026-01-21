/*
*  myfs.c - Implementacao do sistema de arquivos MyFS
*
*  Autores: SUPER_PROGRAMADORES_C
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include <stdio.h>
#include <stdlib.h>
#include "myfs.h"
#include "vfs.h"
#include "inode.h"
#include "util.h"

//Declaracoes globais
typedef struct {
	int used; // 1 = em uso, 0 = livre
	int isDir; // 1 = diretorio, 0 = arquivo
	unsigned int inodeNumber; // numero do inode 
	unsigned int cursor; // posicao do cursor, em bytes
} MyFSFileDescriptor; 



static MyFSFileDescriptor fdTable[MAX_FDS]; //max_fds é uma variavel la do vfs.h que define o numero maximo de descritores de arquivos que podem ser abertos ao mesmo tempo, q é 128
static unsigned int openCount = 0; //esse eh o total de descritores abertos



//Funcao para verificacao se o sistema de arquivos está ocioso, ou seja,
//se nao ha quisquer descritores de arquivos em uso atualmente. Retorna
//um positivo se ocioso ou, caso contrario, 0.
int myFSIsIdle (Disk *d) {
	if (openCount == 0) return 1;  //se não tiver descritores abertos (em uso), está ocioso e por isso retorna 1
		
	//agora vou checar a tabela de descritores pra ver se algum está em uso, pra garantir a consistencia, e se tiver em uso, retorna 0
	for (int i = 0; i < MAX_FDS; i++) {
		if (fdTable[i].used == 1) return 0; 
	}

	return 1; //se chegou aqui, é pq nenhum descritor está em uso, então retorna 1
}

//Funcao para formatacao de um disco com o novo sistema de arquivos
//com tamanho de blocos igual a blockSize. Retorna o numero total de
//blocos disponiveis no disco, se formatado com sucesso. Caso contrario,
//retorna -1.
int myFSFormat (Disk *d, unsigned int blockSize) {
	return -1;
}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount (Disk *d, int x) {
	return 0;
}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen (Disk *d, const char *path) {
	return -1;
}
	
//Funcao para a leitura de um arquivo, a partir de um descritor de arquivo
//existente. Os dados devem ser lidos a partir da posicao atual do cursor
//e copiados para buf. Terao tamanho maximo de nbytes. Ao fim, o cursor
//deve ter posicao atualizada para que a proxima operacao ocorra a partir
//do próximo byte apos o ultimo lido. Retorna o numero de bytes
//efetivamente lidos em caso de sucesso ou -1, caso contrario.
int myFSRead (int fd, char *buf, unsigned int nbytes) {
	// Verifica se o descritor é válido e está em uso
	if (fd < 0 || fd >= MAX_FDS || fdTable[fd].used == 0 || fdTable[fd].isDir == 1) {
		return -1;
	}

	unsigned int inodeNumber = fdTable[fd].inodeNumber;
	unsigned int cursor = fdTable[fd].cursor;

	// Carrega o inode do arquivo
	Inode *inode = inodeLoad(inodeNumber, NULL); // O Disk* pode ser recuperado conforme implementação
	if (inode == NULL) {
		return -1;
	}

	unsigned int fileSize = inodeGetFileSize(inode);
	if (cursor >= fileSize) {
		return 0; // Nada para ler
	}

	unsigned int bytesToRead = nbytes;
	if (cursor + nbytes > fileSize) {
		bytesToRead = fileSize - cursor;
	}

	unsigned int bytesRead = 0;
	unsigned int blockSize = DISK_SECTORDATASIZE; // Tamanho do setor
	unsigned int blockNum = cursor / blockSize;
	unsigned int blockOffset = cursor % blockSize;

	while (bytesRead < bytesToRead) {
		unsigned int blockAddr = inodeGetBlockAddr(inode, blockNum);
		if (blockAddr == 0) {
			break;
		}

		unsigned char sectorData[DISK_SECTORDATASIZE];
		if (diskReadSector(NULL, blockAddr, sectorData) != 0) { // Disk* deve ser recuperado
			break;
		}

		unsigned int toCopy = blockSize - blockOffset;
		if (toCopy > (bytesToRead - bytesRead)) {
			toCopy = bytesToRead - bytesRead;
		}

		memcpy(buf + bytesRead, sectorData + blockOffset, toCopy);
		bytesRead += toCopy;
		blockNum++;
		blockOffset = 0;
	}

	fdTable[fd].cursor += bytesRead;

	if (bytesRead == 0) {
		return -1;
	}

	return bytesRead;
}

//Funcao para a escrita de um arquivo, a partir de um descritor de arquivo
//existente. Os dados de buf sao copiados para o disco a partir da posição
//atual do cursor e terao tamanho maximo de nbytes. Ao fim, o cursor deve
//ter posicao atualizada para que a proxima operacao ocorra a partir do
//proximo byte apos o ultimo escrito. Retorna o numero de bytes
//efetivamente escritos em caso de sucesso ou -1, caso contrario
int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
	// Verifica se o descritor é válido e está em uso
	if (fd < 0 || fd >= MAX_FDS || fdTable[fd].used == 0 || fdTable[fd].isDir == 1) {
		return -1;
	}

	unsigned int inodeNumber = fdTable[fd].inodeNumber;
	unsigned int cursor = fdTable[fd].cursor;

	// Carrega o inode do arquivo
	Inode *inode = inodeLoad(inodeNumber, NULL); // O Disk* pode ser recuperado conforme implementação
	if (inode == NULL) {
		return -1;
	}

	unsigned int fileSize = inodeGetFileSize(inode);
	unsigned int blockSize = DISK_SECTORDATASIZE;
	unsigned int bytesToWrite = nbytes;

	unsigned int bytesWritten = 0;
	unsigned int blockNum = cursor / blockSize;
	unsigned int blockOffset = cursor % blockSize;

	while (bytesWritten < bytesToWrite) {
		unsigned int blockAddr = inodeGetBlockAddr(inode, blockNum);
		if (blockAddr == 0) {
			// Se não há bloco, pode ser necessário alocar um novo bloco (não implementado aqui)
			break;
		}

		unsigned char sectorData[DISK_SECTORDATASIZE];
		if (diskReadSector(NULL, blockAddr, sectorData) != 0) { // Disk* deve ser recuperado
			break;
		}

		unsigned int toCopy = blockSize - blockOffset;
		if (toCopy > (bytesToWrite - bytesWritten)) {
			toCopy = bytesToWrite - bytesWritten;
		}

		memcpy(sectorData + blockOffset, buf + bytesWritten, toCopy);
		if (diskWriteSector(NULL, blockAddr, sectorData) != 0) {
			break;
		}

		bytesWritten += toCopy;
		blockNum++;
		blockOffset = 0;
	}

	fdTable[fd].cursor += bytesWritten;

	// Atualiza o tamanho do arquivo se necessário
	if (cursor + bytesWritten > fileSize) {
		inodeSetFileSize(inode, cursor + bytesWritten);
		inodeSave(inode);
	}

	if (bytesWritten == 0) {
		return -1;
	}

	return bytesWritten;
}

//Funcao para fechar um arquivo, a partir de um descritor de arquivo
//existente. Retorna 0 caso bem sucedido, ou -1 caso contrario
int myFSClose (int fd) {
	return -1;
}

//Funcao para abertura de um diretorio, a partir do caminho
//especificado em path, no disco indicado por d, no modo Read/Write,
//criando o diretorio se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpenDir (Disk *d, const char *path) {
	//qm for fzr esse, lembra de ao entregar um fd, marcar fileDescriptors[fd-1].used = 1
	return -1;
}

//Funcao para a leitura de um diretorio, identificado por um descritor
//de arquivo existente. Os dados lidos correspondem a uma entrada de
//diretorio na posicao atual do cursor no diretorio. O nome da entrada
//e' copiado para filename, como uma string terminada em \0 (max 255+1).
//O numero do inode correspondente 'a entrada e' copiado para inumber.
//Retorna 1 se uma entrada foi lida, 0 se fim de diretorio ou -1 caso
//mal sucedido
int myFSReadDir (int fd, char *filename, unsigned int *inumber) {
	return -1;
}

//Funcao para adicionar uma entrada a um diretorio, identificado por um
//descritor de arquivo existente. A nova entrada tera' o nome indicado
//por filename e apontara' para o numero de i-node indicado por inumber.
//Retorna 0 caso bem sucedido, ou -1 caso contrario.
int myFSLink (int fd, const char *filename, unsigned int inumber) {
	return -1;
}

//Funcao para remover uma entrada existente em um diretorio, 
//identificado por um descritor de arquivo existente. A entrada e'
//identificada pelo nome indicado em filename. Retorna 0 caso bem
//sucedido, ou -1 caso contrario.
int myFSUnlink (int fd, const char *filename) {
	return -1;
}

//Funcao para fechar um diretorio, identificado por um descritor de
//arquivo existente. Retorna 0 caso bem sucedido, ou -1 caso contrario.	
int myFSCloseDir (int fd) {
	return -1;
}

//Funcao para instalar seu sistema de arquivos no S.O., registrando-o junto
//ao virtual FS (vfs). Retorna um identificador unico (slot), caso
//o sistema de arquivos tenha sido registrado com sucesso.
//Caso contrario, retorna -1
int installMyFS (void) {
	FSInfo fsInfo;
	fsInfo.fsid = 1;
	fsInfo.fsname = "MyFS";
	fsInfo.isidleFn = myFSIsIdle;
	fsInfo.formatFn = myFSFormat;
	fsInfo.xMountFn = myFSxMount;
	fsInfo.openFn = myFSOpen;
	fsInfo.readFn = myFSRead;
	fsInfo.writeFn = myFSWrite;
	fsInfo.closeFn = myFSClose;
	fsInfo.opendirFn = myFSOpenDir;
	fsInfo.readdirFn = myFSReadDir;
	fsInfo.linkFn = myFSLink;
	fsInfo.unlinkFn = myFSUnlink;
	fsInfo.closedirFn = myFSCloseDir;
	return vfsRegisterFS(&fsInfo);
}
