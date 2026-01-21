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
#include <string.h>
#include "myfs.h"
#include "vfs.h"
#include "inode.h"
#include "util.h"

// Declarações de funções auxiliares privadas
static int __saveSuperblock(Disk *d, Superblock *sb);
static int __loadSuperblock(Disk *d, Superblock *sb);
static unsigned int __findInodeInDir(Disk *d, unsigned int dirInodeNum, const char *filename);
static int __addEntryToDir(Disk *d, unsigned int dirInodeNum, unsigned int fileInodeNum, const char *filename, Superblock *sb);

//Declaracoes globais
typedef struct {
	int used; // 1 = em uso, 0 = livre
	int isDir; // 1 = diretorio, 0 = arquivo
	unsigned int inodeNumber; // numero do inode 
	unsigned int cursor; // posicao do cursor, em bytes
} MyFSFileDescriptor; 



static MyFSFileDescriptor fdTable[MAX_FDS]; //max_fds é uma variavel la do vfs.h que define o numero maximo de descritores de arquivos que podem ser abertos ao mesmo tempo, q é 128
static unsigned int openCount = 0; //esse eh o total de descritores abertos
static Superblock sb;

// Implementação das funções auxiliares
static int __saveSuperblock(Disk *d, Superblock *sb) {
    unsigned char sector[DISK_SECTORDATASIZE];
    memset(sector, 0, DISK_SECTORDATASIZE);
    
    // Converter estrutura Superblock para bytes e escrever no setor 0
    unsigned char *ptr = sector;
    ul2char(sb->magic, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->blockSize, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->numBlocks, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->freeMapSector, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->freeMapSize, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->dataStartSector, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    ul2char(sb->rootInode, (unsigned char*)ptr); ptr += sizeof(unsigned int);
    
    return diskWriteSector(d, 0, sector);
}

static int __loadSuperblock(Disk *d, Superblock *sb) {
    unsigned char sector[DISK_SECTORDATASIZE];
    
    if (diskReadSector(d, 0, sector) < 0) return -1;
    
    // Converter bytes para estrutura Superblock
    unsigned char *ptr = sector;
    char2ul(ptr, &sb->magic); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->blockSize); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->numBlocks); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->freeMapSector); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->freeMapSize); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->dataStartSector); ptr += sizeof(unsigned int);
    char2ul(ptr, &sb->rootInode); ptr += sizeof(unsigned int);
    
    return 0;
}

static unsigned int __findInodeInDir(Disk *d, unsigned int dirInodeNum, const char *filename) {
    // Função stub - implementação futura
    return 0;
}

static int __addEntryToDir(Disk *d, unsigned int dirInodeNum, unsigned int fileInodeNum, const char *filename, Superblock *sb) {
    // Função stub - implementação futura
    return 0;
}


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
    printf("[DEBUG] Iniciando myFSFormat.\n");

    // Check 1: Tamanho do bloco
    if (blockSize == 0 || (blockSize % DISK_SECTORDATASIZE != 0)) {
        printf("[DEBUG] ERRO: BlockSize invalido (%u). Deve ser multiplo de %d.\n", blockSize, DISK_SECTORDATASIZE);
        return -1;
    }

    unsigned long totalSectors = diskGetNumSectors(d);
    printf("[DEBUG] Total de setores no disco: %lu\n", totalSectors);

    // Se o disco for muito pequeno (ex: erro no Build), totalSectors pode ser 0
    if (totalSectors < 100) {
         printf("[DEBUG] ERRO: Disco muito pequeno ou nao inicializado corretamente.\n");
         return -1;
    }

    unsigned long inodeTableSizeInBytes = MAX_INODES * (16 * sizeof(unsigned int));
    unsigned long inodeTableSectors = (inodeTableSizeInBytes + DISK_SECTORDATASIZE - 1) / DISK_SECTORDATASIZE;
    
    unsigned long freeMapStartSector = inodeAreaBeginSector() + inodeTableSectors;
    unsigned long availableSectorsForData = totalSectors - freeMapStartSector;
    
    // Check 2: Overflow ou disco pequeno demais para os metadados
    if (freeMapStartSector >= totalSectors) {
        printf("[DEBUG] ERRO: Espaco insuficiente para tabela de inodes.\n");
        return -1;
    }

    unsigned long numBlocks = (availableSectorsForData * DISK_SECTORDATASIZE) / blockSize;
    
    // Check 3: Numero de blocos zero
    if (numBlocks == 0) {
        printf("[DEBUG] ERRO: Numero de blocos calculado resultou em 0.\n");
        return -1;
    }

    unsigned long bitmapBytes = (numBlocks + 7) / 8;
    unsigned long bitmapSectors = (bitmapBytes + DISK_SECTORDATASIZE - 1) / DISK_SECTORDATASIZE;

    unsigned long dataStartSector = freeMapStartSector + bitmapSectors;
    
    printf("[DEBUG] Data Start Sector: %lu\n", dataStartSector);

    // Check 4: Metadados ocupam todo o disco
    if (dataStartSector >= totalSectors) {
        printf("[DEBUG] ERRO: Metadados (Bitmap+Inodes) ocupam todo o disco.\n");
        return -1;
    }
    
    // Recalculo final
    numBlocks = ((totalSectors - dataStartSector) * DISK_SECTORDATASIZE) / blockSize;
    printf("[DEBUG] Formatacao valida. Criando %lu blocos.\n", numBlocks);

    unsigned char emptySector[DISK_SECTORDATASIZE];
    memset(emptySector, 0, DISK_SECTORDATASIZE);

    // Limpezas
    for (unsigned long i = 0; i < inodeTableSectors; i++) {
        if (diskWriteSector(d, inodeAreaBeginSector() + i, emptySector) < 0) {
             printf("[DEBUG] ERRO: Falha ao limpar setor de inode %lu.\n", inodeAreaBeginSector() + i);
             return -1;
        }
    }

    for (unsigned long i = 0; i < bitmapSectors; i++) {
        diskWriteSector(d, freeMapStartSector + i, emptySector);
    }

    // Superbloco
    Superblock sb;
    sb.magic = MYFS_MAGIC;
    sb.blockSize = blockSize;
    sb.numBlocks = numBlocks;
    sb.freeMapSector = freeMapStartSector;
    sb.freeMapSize = bitmapSectors;
    sb.dataStartSector = dataStartSector;
    sb.rootInode = 1;

    // Check 5: Salvar Superbloco
    if (__saveSuperblock(d, &sb) < 0) {
        printf("[DEBUG] ERRO: Falha ao gravar o Superbloco.\n");
        return -1;
    }

    // Check 6: Criar Inode Raiz
    Inode *root = inodeCreate(1, d);
    if (!root) {
        printf("[DEBUG] ERRO: Falha ao criar o Inode Raiz (inodeCreate retornou NULL).\n");
        return -1;
    }
    
    inodeSetFileType(root, FILETYPE_DIR);
    inodeSetOwner(root, 1000); 
    inodeSetFileSize(root, 0); 
    
    if (inodeSave(root) < 0) {
         printf("[DEBUG] ERRO: Falha ao salvar o Inode Raiz no disco.\n");
         free(root);
         return -1;
    }
    free(root);

    printf("[DEBUG] Sucesso! Retornando %lu blocos.\n", numBlocks);
    return numBlocks;
}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount (Disk *d, int x) {
    if (x == 1) { 
        
        
        //carrega o Superbloco do disco para a memória
        if (__loadSuperblock(d, &sb) < 0) {
            return 0; // falha na leitura
        }

        //verificação de sanidade (Magic Number)
        //garante que o disco foi formatado com MyFS antes de montar
        if (sb.magic != MYFS_MAGIC) {
            return 0; // disco não formatado ou corrompido
        }

        //inicializa as estruturas em memória (Limpa tabela de descritores)
        //garante que não haja lixo de execuções anteriores
        for (int i = 0; i < MAX_FDS; i++) {
            fdTable[i].used = 0;
            fdTable[i].inodeNumber = 0;
            fdTable[i].cursor = 0;
        }
        openCount = 0;

        return 1; // deu tudo certo na montagem

    } else {
       // desmonta
        
        // persistir o Superbloco
        // se houver mapas de bits ou contadores alterados, salva
        if (__saveSuperblock(d, &sb) < 0) {
            return 0; // falha na escrita
        }


        return 1; // deu tudo certo na desmontagem
    }
}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen (Disk *d, const char *path) {
    // 1. Verificações Básicas
    if (!d || !path) return -1;
    if (sb.magic != MYFS_MAGIC) return -1; // Sistema não montado ou inválido
    if (openCount >= MAX_FDS) return -1;   // Tabela cheia

    // 2. Cópia do path (strtok modifica a string original)
    char pathCopy[MAX_FILENAME_LENGTH + 1];
    strncpy(pathCopy, path, MAX_FILENAME_LENGTH);
    pathCopy[MAX_FILENAME_LENGTH] = '\0';

    if (strcmp(pathCopy, "/") == 0) return -1; // Não pode abrir raiz como arquivo

    // 3. Navegação pelos diretórios
    unsigned int currentInode = sb.rootInode;
    unsigned int parentInode = 0;
    
    char *token = strtok(pathCopy, "/");
    char filename[MAX_FILENAME_LENGTH + 1];

    while (token != NULL) {
        strncpy(filename, token, MAX_FILENAME_LENGTH);
        filename[MAX_FILENAME_LENGTH] = '\0';
        
        parentInode = currentInode;
        
        // Procura o token atual no diretório corrente
        unsigned int nextInode = __findInodeInDir(d, currentInode, filename);
        
        // Verifica o próximo token para saber se este é o último (arquivo) ou pasta
        char *nextToken = strtok(NULL, "/");

        if (nextInode != 0) {
            // Encontrou: desce para o próximo nível
            currentInode = nextInode;
        } else {
            // Não encontrou
            if (nextToken == NULL) {
                // É o último item do caminho (o arquivo). Devemos CRIAR.
                
                // A. Encontra inode livre no disco
                unsigned int freeInodeNum = inodeFindFreeInode(1, d);
                if (freeInodeNum == 0) return -1; // Disco cheio

                // B. Cria e configura o novo inode
                Inode *newFile = inodeCreate(freeInodeNum, d);
                if (!newFile) return -1;

                inodeSetFileType(newFile, FILETYPE_REGULAR);
                inodeSetOwner(newFile, 0); // Define dono padrão (root/0)
                inodeSetFileSize(newFile, 0);
                
                if (inodeSave(newFile) < 0) {
                    free(newFile);
                    return -1;
                }
                free(newFile);

                // C. Adiciona entrada no diretório pai
                if (__addEntryToDir(d, parentInode, freeInodeNum, filename, &sb) < 0) {
                    return -1; // Falha ao escrever no diretório
                }
                
                currentInode = freeInodeNum; // O arquivo aberto é o novo inode
            } else {
                // Caminho inválido (diretório intermediário não existe)
                return -1;
            }
        }
        token = nextToken;
    }

    // 4. Verificação Final: É arquivo mesmo?
    Inode *targetInode = inodeLoad(currentInode, d);
    if (!targetInode) return -1;

    if (inodeGetFileType(targetInode) == FILETYPE_DIR) {
        free(targetInode);
        return -1; // Erro: Tentou abrir diretório com myFSOpen (use myFSOpenDir)
    }
    free(targetInode);

    // 5. Alocação na Tabela de Descritores (fdTable)
    for (int i = 0; i < MAX_FDS; i++) {
        if (fdTable[i].used == 0) {
            fdTable[i].used = 1;
            fdTable[i].isDir = 0;
            fdTable[i].inodeNumber = currentInode;
            fdTable[i].cursor = 0;
            openCount++;
            return i + 1; // Retorna FD (índice + 1, padrão POSIX/VFS)
        }
    }

    return -1; // Caso raro: openCount estava ok, mas não achou slot (inconsistência)
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
	// conversão do FD do VFS (1-based) para o índice do array (0-based)
    int index = fd - 1;

    // verificação de limites: O índice é válido dentro do array?
    if (index < 0 || index >= MAX_FDS) {
        return -1;
    }

    // verificação de estado: O arquivo está realmente aberto?
    // se used for 0, significa que este FD já está fechado ou nunca foi usado.
    if (fdTable[index].used == 0) {
        return -1;
    }

    // verifica se é não é um diretório
    // se quiser ser estrito: if (fdTable[index].isDir == 1) return -1;
    
    //"fecha" o arquivo limpando a flag de uso
    fdTable[index].used = 0;

    
    fdTable[index].inodeNumber = 0;
    fdTable[index].cursor = 0;
    fdTable[index].isDir = 0;

    //decrementa o contador global de arquivos abertos
    if (openCount > 0) {
        openCount--;
    }

    return 0;
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
static FSInfo fsInfo; // Tive que mudar aqui pra static e colocar fora da funcao pra nao perder o valor apos o retorno
int installMyFS (void) {
	//FSInfo fsInfo;
    memset(&fsInfo, 0, sizeof(FSInfo));
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
