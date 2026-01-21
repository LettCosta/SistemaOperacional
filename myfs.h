/*
*  myfs.h - Funcao que permite a instalacao de seu sistema de arquivos no S.O.
*
*  Autor: SUPER_PROGRAMADORES C
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*
*/

#ifndef MYFS_H
#define MYFS_H

#include "vfs.h"
#include <string.h>

// Constantes do sistema de arquivos MyFS
#define MYFS_MAGIC 0x12345678
#define MAX_INODES 1024

// Estrutura do Superbloco
typedef struct {
    unsigned int magic;
    unsigned int blockSize;
    unsigned int numBlocks;
    unsigned int freeMapSector;
    unsigned int freeMapSize;
    unsigned int dataStartSector;
    unsigned int rootInode;
} Superblock;

//Funcao para instalar seu sistema de arquivos no S.O., registrando-o junto
//ao virtual FS (vfs). Retorna um identificador unico (slot), caso
//o sistema de arquivos tenha sido registrado com sucesso.
//Caso contrario, retorna -1
int installMyFS ( void );

#endif
