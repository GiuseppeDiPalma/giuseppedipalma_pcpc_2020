#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>
#include <stddef.h>

#include "utils.h"
#include "fileManage.h"
#include "wordManage.h"

#define TOTALWORDS 4000000 // questa variabile dovrebbe essere dinamica
#define SPLIT_PROCESSOR 1000

void main(int argc, char *argv[])
{
    int rank, size;
    int tag = 1;
    int source = 0;
    int numSplits = 0;
    char *dirFile;
    int wordsForProcessor[size];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Request rq = MPI_REQUEST_NULL;
    MPI_Status stat;

    //Parameters for time MPI
    double start = MPI_Wtime();

    PartitionedWord *n_words = (PartitionedWord *)malloc(sizeof(PartitionedWord) * SPLIT_PROCESSOR);
    Word *wds = (Word *)malloc(sizeof(Word) * TOTALWORDS);

    parse_arg(argc, argv, &dirFile);
    int numFiles = countFilesInDirectory(dirFile);

    FileWordSize fileSpec[numFiles];
    int sumWord = readFilesAndSum(dirFile, fileSpec);

    // ! DA QUI
    int count = 4;
    int blockcounts[count], blockcounts1[2];

    MPI_Datatype wordtype, filePerProcType, oldType[count], oldType1[2];
    MPI_Aint offsets[count], offsets1[2], lb, extent;

    offsets[0] = offsetof(PartitionedWord, rank);
    oldType[0] = MPI_INT;
    blockcounts[0] = 1;
    MPI_Type_get_extent(MPI_INT, &lb, &extent);

    offsets[1] = offsetof(PartitionedWord, start);
    oldType[1] = MPI_INT;
    blockcounts[1] = 1;
    MPI_Type_get_extent(MPI_INT, &lb, &extent);

    offsets[2] = offsetof(PartitionedWord, fileName);
    oldType[2] = MPI_CHAR;
    blockcounts[2] = MAXFILENAME;
    MPI_Type_get_extent(MPI_CHAR, &lb, &extent);

    offsets[3] = offsetof(PartitionedWord, end);
    oldType[3] = MPI_INT;
    blockcounts[3]  = 1;
    MPI_Type_get_extent(MPI_INT, &lb, &extent);

    //create type struct
    MPI_Type_create_struct(count, blockcounts, offsets, oldType, &filePerProcType);
    MPI_Type_commit(&filePerProcType);

    //space first field and second field
    offsets1[0] = offsetof(Word, freq);
    oldType1[0] = MPI_INT;
    blockcounts1[0] = 1;
    MPI_Type_get_extent(MPI_INT, &lb, &extent);

    //save offset value after first field
    offsets1[1] = offsetof(Word, word);
    oldType1[1] = MPI_CHAR;
    blockcounts1[1] = WORDMAXSIZE;
    MPI_Type_get_extent(MPI_CHAR, &lb, &extent);


    //create struct with fields
    MPI_Type_create_struct(2, blockcounts1, offsets1, oldType1, &wordtype);
    MPI_Type_commit(&wordtype);
    // ! A QUI

    if (rank == 0)
    {
        printf("Total Words [%d]\n", sumWord);

        elementSplit(wordsForProcessor, sumWord, size);

        for (int i = 0; i < size; i++)
        {
            printf("proc [%d] - processa [%d] parole\n", i, wordsForProcessor[i]);
        }

        for (int i = 0; i < numFiles; i++)
        {
            printf("file name: %s - %d parole totali\n", fileSpec[i].fileName, fileSpec[i].wordNumber);
        }

        numSplits = wordForProcessor(n_words, wordsForProcessor, fileSpec, size);
        printf("numSplitsxx: %d\n", numSplits);

        for (int i = 0; i < numSplits; i++)
        {
            printf("Struttura %d relativa al file %s --> Proc: %d - Start: %d -> End: %d\n", i,
                   n_words[i].fileName, n_words[i].rank, n_words[i].start, n_words[i].end);
        }

        int iStruct = 0; //indice di dove mi trovo all'interno della struttura
        int startForZero = 0;
        int sizeOfZero = 0;

        //celle da passare a processo 1...
        while (n_words[iStruct].rank == 0)
        {
            iStruct++;
            startForZero++;
        }

        int base = -1;
        int size_send = -1;
        for (int i = 1; i < size; i++)
        {
            size_send = 0;  //quanti elementi
            base = iStruct; // Da dove parto
            while (n_words[iStruct].rank == i)
            {
                iStruct++;
                size_send++;
            }
            MPI_Send(&n_words[base], size_send, filePerProcType, i, tag, MPI_COMM_WORLD);
            printf("Sending %d to %d struct to %d\n", base, base + size_send - 1, i);
        }

        sizeOfZero = copyLineInStruct(wds, n_words, startForZero);
        wordsCount(wds, sizeOfZero);

        int sizeForProcessor = 0;
        int start2 = sizeOfZero;
        int quant = TOTALWORDS - start2;

        for (int p = 1; p < size; p++)
        {
            MPI_Recv(&wds[start2], quant, wordtype, p, tag, MPI_COMM_WORLD, &stat);
            MPI_Get_count(&stat, wordtype, &sizeForProcessor);
            
            printf("Received %d words from proc %d\n", sizeForProcessor, p);
            // TODO: Sommo ai miei risultati
            start2 = start2 + sizeForProcessor;
            quant = TOTALWORDS - start2;
        }
        printf("start2: %d\n", start2);
        writeResultCSV(wds, start2);
    }
    else
    {
        int count = 0;
        int structSize = 0;
        MPI_Recv(n_words, SPLIT_PROCESSOR, filePerProcType, source, tag, MPI_COMM_WORLD, &stat);
        MPI_Get_count(&stat, filePerProcType, &count);

        structSize = copyLineInStruct(wds, n_words, count);
        wordsCount(wds, structSize);
        MPI_Ssend(wds, structSize, wordtype, 0, tag, MPI_COMM_WORLD);
    }

    //Parameters for time and result MPI
    double end = MPI_Wtime();
    double totalTimeMPI = end - start;
    printf("Total time ---> %lf seconds\n", totalTimeMPI);

    MPI_Type_free(&wordtype);
    MPI_Type_free(&filePerProcType);
    MPI_Finalize();
}