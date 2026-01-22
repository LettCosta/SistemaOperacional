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
    Disk *d; // ponteiro para o disco associado
} MyFSFileDescriptor; 

static MyFSFileDescriptor fdTable[MAX_FDS];
static unsigned int openCount = 0;
static Superblock sb;

// Implementação das funções auxiliares
static int __saveSuperblock(Disk *d, Superblock *sb) {
    unsigned char sector[DISK_SECTORDATASIZE];
    memset(sector, 0, DISK_SECTORDATASIZE);
    
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

static unsigned int __allocBlock(Disk *d, Superblock *sb) {
    unsigned char buffer[DISK_SECTORDATASIZE];
    unsigned int totalBits = sb->numBlocks;
    unsigned int bitCounter = 0;

    for (unsigned int s = 0; s < sb->freeMapSize; s++) {
        if (diskReadSector(d, sb->freeMapSector + s, buffer) < 0) return 0;
        
        for (int i = 0; i < DISK_SECTORDATASIZE; i++) {
            for (int bit = 0; bit < 8; bit++) {
                if (bitCounter >= totalBits) return 0; 
                
                if (!((buffer[i] >> bit) & 1)) {
                    buffer[i] |= (1 << bit);
                    diskWriteSector(d, sb->freeMapSector + s, buffer);
                    
                    unsigned int sectorsPerBlock = sb->blockSize / DISK_SECTORDATASIZE;
                    return sb->dataStartSector + (bitCounter * sectorsPerBlock);
                }
                bitCounter++;
            }
        }
    }
    return 0;
}

static unsigned int __findInodeInDir(Disk *d, unsigned int dirInodeNum, const char *filename) {
    Inode *dirInode = inodeLoad(dirInodeNum, d);
    if (!dirInode) return 0;
    
    if (inodeGetFileType(dirInode) != FILETYPE_DIR) {
        free(dirInode);
        return 0;
    }
    
    unsigned int dirSize = inodeGetFileSize(dirInode);
    if (dirSize == 0) {
        free(dirInode);
        return 0;
    }
    
    unsigned int entrySize = sizeof(unsigned int) + MAX_FILENAME_LENGTH + 1;
    unsigned int numEntries = dirSize / entrySize;
    
    // Usa o superbloco global
    unsigned int blockSize = sb.blockSize;
    
    for (unsigned int i = 0; i < numEntries; i++) {
        unsigned int offset = i * entrySize;
        unsigned char entryBuffer[sizeof(unsigned int) + MAX_FILENAME_LENGTH + 1];
        unsigned int bytesRead = 0;
        
        while (bytesRead < entrySize) {
            unsigned int logicalBlockNum = (offset + bytesRead) / blockSize;
            unsigned int offsetInBlock = (offset + bytesRead) % blockSize;
            
            unsigned int blockAddr = inodeGetBlockAddr(dirInode, logicalBlockNum);
            if (blockAddr == 0) break;
            
            // Calcula qual setor dentro do bloco
            unsigned int sectorInBlock = offsetInBlock / DISK_SECTORDATASIZE;
            unsigned int offsetInSector = offsetInBlock % DISK_SECTORDATASIZE;
            
            unsigned char sectorData[DISK_SECTORDATASIZE];
            if (diskReadSector(d, blockAddr + sectorInBlock, sectorData) < 0) break;
            
            unsigned int toCopy = DISK_SECTORDATASIZE - offsetInSector;
            if (toCopy > (entrySize - bytesRead)) {
                toCopy = entrySize - bytesRead;
            }
            
            memcpy(entryBuffer + bytesRead, sectorData + offsetInSector, toCopy);
            bytesRead += toCopy;
        }
        
        if (bytesRead < entrySize) break;
        
        unsigned int inodeNum;
        char entryName[MAX_FILENAME_LENGTH + 1];
        
        char2ul(entryBuffer, &inodeNum);
        memcpy(entryName, entryBuffer + sizeof(unsigned int), MAX_FILENAME_LENGTH + 1);
        entryName[MAX_FILENAME_LENGTH] = '\0';
        
        if (strcmp(entryName, filename) == 0) {
            free(dirInode);
            return inodeNum;
        }
    }
    
    free(dirInode);
    return 0;
}

static int __addEntryToDir(Disk *d, unsigned int dirInodeNum, unsigned int fileInodeNum, const char *filename, Superblock *sb) {
    Inode *dirInode = inodeLoad(dirInodeNum, d);
    if (!dirInode) return -1;
    
    if (inodeGetFileType(dirInode) != FILETYPE_DIR) {
        free(dirInode);
        return -1;
    }
    
    unsigned int entrySize = sizeof(unsigned int) + MAX_FILENAME_LENGTH + 1;
    unsigned char entryBuffer[entrySize];
    memset(entryBuffer, 0, entrySize);
    
    ul2char(fileInodeNum, entryBuffer);
    strncpy((char*)(entryBuffer + sizeof(unsigned int)), filename, MAX_FILENAME_LENGTH);
    
    unsigned int dirSize = inodeGetFileSize(dirInode);
    unsigned int offset = dirSize;
    unsigned int bytesWritten = 0;
    
    while (bytesWritten < entrySize) {
        unsigned int blockNum = offset / sb->blockSize;
        unsigned int blockOffset = offset % sb->blockSize;
        
        unsigned int blockAddr = inodeGetBlockAddr(dirInode, blockNum);
        
        if (blockAddr == 0) {
            blockAddr = __allocBlock(d, sb);
            if (blockAddr == 0) {
                free(dirInode);
                return -1;
            }
            
            if (inodeAddBlock(dirInode, blockAddr) < 0) {
                free(dirInode);
                return -1;
            }
            
            unsigned char *cleanBuf = calloc(1, sb->blockSize);
            if (!cleanBuf) {
                free(dirInode);
                return -1;
            }
            
            int sectorsPerBlock = sb->blockSize / DISK_SECTORDATASIZE;
            for (int k = 0; k < sectorsPerBlock; k++) {
                if (diskWriteSector(d, blockAddr + k, cleanBuf + (k * DISK_SECTORDATASIZE)) < 0) {
                    free(cleanBuf);
                    free(dirInode);
                    return -1;
                }
            }
            free(cleanBuf);
        }
        
        unsigned char *blockBuffer = malloc(sb->blockSize);
        if (!blockBuffer) {
            free(dirInode);
            return -1;
        }
        
        int sectorsPerBlock = sb->blockSize / DISK_SECTORDATASIZE;
        
        for (int k = 0; k < sectorsPerBlock; k++) {
            if (diskReadSector(d, blockAddr + k, blockBuffer + (k * DISK_SECTORDATASIZE)) < 0) {
                free(blockBuffer);
                free(dirInode);
                return -1;
            }
        }
        
        unsigned int spaceInBlock = sb->blockSize - blockOffset;
        unsigned int toCopy = entrySize - bytesWritten;
        if (toCopy > spaceInBlock) toCopy = spaceInBlock;
        
        memcpy(blockBuffer + blockOffset, entryBuffer + bytesWritten, toCopy);
        
        for (int k = 0; k < sectorsPerBlock; k++) {
            if (diskWriteSector(d, blockAddr + k, blockBuffer + (k * DISK_SECTORDATASIZE)) < 0) {
                free(blockBuffer);
                free(dirInode);
                return -1;
            }
        }
        
        free(blockBuffer);
        
        bytesWritten += toCopy;
        offset += toCopy;
    }
    
    inodeSetFileSize(dirInode, dirSize + entrySize);
    if (inodeSave(dirInode) < 0) {
        free(dirInode);
        return -1;
    }
    
    free(dirInode);
    return 0;
}

int myFSIsIdle (Disk *d) {
    if (openCount == 0) return 1;
        
    for (int i = 0; i < MAX_FDS; i++) {
        if (fdTable[i].used == 1) return 0; 
    }

    return 1;
}

int myFSFormat (Disk *d, unsigned int blockSize) {
    if (blockSize == 0 || (blockSize % DISK_SECTORDATASIZE != 0)) {
        return -1;
    }

    unsigned long totalSectors = diskGetNumSectors(d);

    if (totalSectors < 100) {
         return -1;
    }

    unsigned long inodeTableSizeInBytes = MAX_INODES * (16 * sizeof(unsigned int));
    unsigned long inodeTableSectors = (inodeTableSizeInBytes + DISK_SECTORDATASIZE - 1) / DISK_SECTORDATASIZE;
    
    unsigned long freeMapStartSector = inodeAreaBeginSector() + inodeTableSectors;
    unsigned long availableSectorsForData = totalSectors - freeMapStartSector;
    
    if (freeMapStartSector >= totalSectors) {
        return -1;
    }

    unsigned long numBlocks = (availableSectorsForData * DISK_SECTORDATASIZE) / blockSize;
    
    if (numBlocks == 0) {
        return -1;
    }

    unsigned long bitmapBytes = (numBlocks + 7) / 8;
    unsigned long bitmapSectors = (bitmapBytes + DISK_SECTORDATASIZE - 1) / DISK_SECTORDATASIZE;

    unsigned long dataStartSector = freeMapStartSector + bitmapSectors;

    if (dataStartSector >= totalSectors) {
        return -1;
    }
    
    numBlocks = ((totalSectors - dataStartSector) * DISK_SECTORDATASIZE) / blockSize;

    // Inicializa todos os inodes vazios com seus números corretos
    unsigned int inodesPerSector = DISK_SECTORDATASIZE / (16 * sizeof(unsigned int));
    unsigned long inodeAreaBegin = inodeAreaBeginSector();
    
    for (unsigned long sectorIdx = 0; sectorIdx < inodeTableSectors; sectorIdx++) {
        unsigned char sector[DISK_SECTORDATASIZE];
        memset(sector, 0, DISK_SECTORDATASIZE);
        
        // Inicializar cada inode dentro deste setor
        for (unsigned int j = 0; j < inodesPerSector; j++) {
            unsigned int inodeNum = sectorIdx * inodesPerSector + j + 1; // Inodes começam de 1
            if (inodeNum > MAX_INODES) break;
            
            // Calcular offset deste inode no setor
            unsigned int offset = j * 16 * sizeof(unsigned int);
            
            // Escrever o número do inode na posição correta (posição 14, ou seja INODE_SIZE-2)
            ul2char(inodeNum, &sector[offset + 14 * sizeof(unsigned int)]);
            // Posição 15 (next) já é 0 pois memset zerou tudo
        }
        
        if (diskWriteSector(d, inodeAreaBegin + sectorIdx, sector) < 0) {
            return -1;
        }
    }

    // Inicializa o bitmap com zeros
    unsigned char emptySector[DISK_SECTORDATASIZE];
    memset(emptySector, 0, DISK_SECTORDATASIZE);
    for (unsigned long i = 0; i < bitmapSectors; i++) {
        diskWriteSector(d, freeMapStartSector + i, emptySector);
    }

    Superblock sb;
    sb.magic = MYFS_MAGIC;
    sb.blockSize = blockSize;
    sb.numBlocks = numBlocks;
    sb.freeMapSector = freeMapStartSector;
    sb.freeMapSize = bitmapSectors;
    sb.dataStartSector = dataStartSector;
    sb.rootInode = 1;

    if (__saveSuperblock(d, &sb) < 0) {
        return -1;
    }

    Inode *root = inodeCreate(1, d);
    if (!root) {
        return -1;
    }
    
    inodeSetFileType(root, FILETYPE_DIR);
    inodeSetOwner(root, 1000); 
    inodeSetFileSize(root, 0); 
    
    if (inodeSave(root) < 0) {
         free(root);
         return -1;
    }
    free(root);

    return numBlocks;
}

int myFSxMount (Disk *d, int x) {
    if (x == 1) { 
        if (__loadSuperblock(d, &sb) < 0) {
            return 0;
        }

        if (sb.magic != MYFS_MAGIC) {
            return 0;
        }

        for (int i = 0; i < MAX_FDS; i++) {
            fdTable[i].used = 0;
            fdTable[i].inodeNumber = 0;
            fdTable[i].cursor = 0;
        }
        openCount = 0;

        return 1;

    } else {
        if (__saveSuperblock(d, &sb) < 0) {
            return 0;
        }

        return 1;
    }
}

int myFSOpen (Disk *d, const char *path) {
    if (!d || !path) {
        return -1;
    }
    if (sb.magic != MYFS_MAGIC) {
        return -1;
    }
    if (openCount >= MAX_FDS) {
        return -1;
    }

    char pathCopy[MAX_FILENAME_LENGTH + 1];
    strncpy(pathCopy, path, MAX_FILENAME_LENGTH);
    pathCopy[MAX_FILENAME_LENGTH] = '\0';

    if (strcmp(pathCopy, "/") == 0) return -1;

    unsigned int currentInode = sb.rootInode;
    unsigned int parentInode = 0;
    
    char *token = strtok(pathCopy, "/");
    char filename[MAX_FILENAME_LENGTH + 1];

    while (token != NULL) {
        strncpy(filename, token, MAX_FILENAME_LENGTH);
        filename[MAX_FILENAME_LENGTH] = '\0';
        
        parentInode = currentInode;
        
        unsigned int nextInode = __findInodeInDir(d, currentInode, filename);
        char *nextToken = strtok(NULL, "/");

        if (nextInode != 0) {
            currentInode = nextInode;
        } else {
            if (nextToken == NULL) {
                unsigned int freeInodeNum = inodeFindFreeInode(2, d);
                if (freeInodeNum == 0) {
                    return -1;
                }

                Inode *newFile = inodeCreate(freeInodeNum, d);
                if (!newFile) return -1;

                inodeSetFileType(newFile, FILETYPE_REGULAR);
                inodeSetOwner(newFile, 0);
                inodeSetFileSize(newFile, 0);
                
                if (inodeSave(newFile) < 0) {
                    free(newFile);
                    return -1;
                }
                free(newFile);

                if (__addEntryToDir(d, parentInode, freeInodeNum, filename, &sb) < 0) {
                    return -1;
                }
                
                currentInode = freeInodeNum;
            } else {
                return -1;
            }
        }
        token = nextToken;
    }

    Inode *targetInode = inodeLoad(currentInode, d);
    if (!targetInode) return -1;

    if (inodeGetFileType(targetInode) == FILETYPE_DIR) {
        free(targetInode);
        return -1;
    }
    free(targetInode);

    for (int i = 0; i < MAX_FDS; i++) {
        if (fdTable[i].used == 0) {
            fdTable[i].used = 1;
            fdTable[i].isDir = 0;
            fdTable[i].inodeNumber = currentInode;
            fdTable[i].cursor = 0;
            fdTable[i].d = d; 
            openCount++;
            return i + 1;
        }
    }

    return -1;
}
    
int myFSRead (int fd, char *buf, unsigned int nbytes) {
    int idx = fd - 1;
    if (idx < 0 || idx >= MAX_FDS || fdTable[idx].used == 0 || fdTable[idx].isDir == 1) {
        return -1;
    }

    Disk *d = fdTable[idx].d;
    unsigned int inodeNum = fdTable[idx].inodeNumber;

    Superblock sb;
    if (__loadSuperblock(d, &sb) < 0) return -1;

    Inode *inode = inodeLoad(inodeNum, d);
    if (!inode) return -1;

    unsigned int fileSize = inodeGetFileSize(inode);
    unsigned int cursor = fdTable[idx].cursor;
    unsigned int bytesRead = 0;

    if (cursor >= fileSize) {
        free(inode);
        return 0;
    }

    if (cursor + nbytes > fileSize) {
        nbytes = fileSize - cursor;
    }

    while (bytesRead < nbytes) {
        unsigned int logicalBlockNum = cursor / sb.blockSize;
        unsigned int offsetInBlock = cursor % sb.blockSize;
        
        unsigned int physicalBlockAddr = inodeGetBlockAddr(inode, logicalBlockNum);
        
        unsigned char *blockBuffer = malloc(sb.blockSize);
        
        if (physicalBlockAddr != 0) {
            int sectorsPerBlock = sb.blockSize / DISK_SECTORDATASIZE;
            for (int k = 0; k < sectorsPerBlock; k++) {
                diskReadSector(d, physicalBlockAddr + k, blockBuffer + (k * DISK_SECTORDATASIZE));
            }
        } else {
            memset(blockBuffer, 0, sb.blockSize);
        }

        unsigned int spaceInBlock = sb.blockSize - offsetInBlock;
        unsigned int toCopy = nbytes - bytesRead;
        if (toCopy > spaceInBlock) toCopy = spaceInBlock;

        memcpy(buf + bytesRead, blockBuffer + offsetInBlock, toCopy);
        
        free(blockBuffer);

        bytesRead += toCopy;
        cursor += toCopy;
    }

    fdTable[idx].cursor = cursor;
    free(inode);

    return bytesRead;
}

int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
    int idx = fd - 1;
    if (idx < 0 || idx >= MAX_FDS || fdTable[idx].used == 0 || fdTable[idx].isDir == 1) {
        return -1;
    }

    Disk *d = fdTable[idx].d;
    unsigned int inodeNum = fdTable[idx].inodeNumber;
    
    Superblock sb;
    if (__loadSuperblock(d, &sb) < 0) {
        return -1;
    }

    Inode *inode = inodeLoad(inodeNum, d);
    if (!inode) {
        return -1;
    }

    unsigned int bytesWritten = 0;
    unsigned int cursor = fdTable[idx].cursor;
    
    while (bytesWritten < nbytes) {
        unsigned int logicalBlockNum = cursor / sb.blockSize;
        unsigned int offsetInBlock = cursor % sb.blockSize;
        
        unsigned int physicalBlockAddr = inodeGetBlockAddr(inode, logicalBlockNum);

        if (physicalBlockAddr == 0) {
            unsigned int newBlock = __allocBlock(d, &sb);
            if (newBlock == 0) break;

            if (inodeAddBlock(inode, newBlock) < 0) break;
            
            physicalBlockAddr = newBlock;

            unsigned char *cleanBuf = calloc(1, sb.blockSize);
            int sectorsPerBlock = sb.blockSize / DISK_SECTORDATASIZE;
            for (int k = 0; k < sectorsPerBlock; k++) 
                diskWriteSector(d, physicalBlockAddr + k, cleanBuf + (k * DISK_SECTORDATASIZE));
            free(cleanBuf);
        }

        unsigned char *blockBuffer = malloc(sb.blockSize);
        int sectorsPerBlock = sb.blockSize / DISK_SECTORDATASIZE;

        for (int k = 0; k < sectorsPerBlock; k++) {
            diskReadSector(d, physicalBlockAddr + k, blockBuffer + (k * DISK_SECTORDATASIZE));
        }

        unsigned int spaceInBlock = sb.blockSize - offsetInBlock;
        unsigned int toCopy = nbytes - bytesWritten;
        if (toCopy > spaceInBlock) toCopy = spaceInBlock;

        memcpy(blockBuffer + offsetInBlock, buf + bytesWritten, toCopy);

        for (int k = 0; k < sectorsPerBlock; k++) {
            diskWriteSector(d, physicalBlockAddr + k, blockBuffer + (k * DISK_SECTORDATASIZE));
        }

        free(blockBuffer);

        bytesWritten += toCopy;
        cursor += toCopy;
    }

    fdTable[idx].cursor = cursor;

    if (cursor > inodeGetFileSize(inode)) {
        inodeSetFileSize(inode, cursor);
        inodeSave(inode);
    }

    free(inode);

    if (bytesWritten == 0 && nbytes > 0) return -1;
    return bytesWritten;
}

int myFSClose (int fd) {
    int index = fd - 1;

    if (index < 0 || index >= MAX_FDS) {
        return -1;
    }

    if (fdTable[index].used == 0) {
        return -1;
    }
    
    fdTable[index].used = 0;
    fdTable[index].inodeNumber = 0;
    fdTable[index].cursor = 0;
    fdTable[index].isDir = 0;

    if (openCount > 0) {
        openCount--;
    }

    return 0;
}

int myFSOpenDir (Disk *d, const char *path) {
    return -1;
}

int myFSReadDir (int fd, char *filename, unsigned int *inumber) {
    return -1;
}

int myFSLink (int fd, const char *filename, unsigned int inumber) {
    return -1;
}

int myFSUnlink (int fd, const char *filename) {
    return -1;
}

int myFSCloseDir (int fd) {
    return -1;
}

static FSInfo fsInfo;
int installMyFS (void) {
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