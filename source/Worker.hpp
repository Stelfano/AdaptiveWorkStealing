#include<vector>
#include<atomic>
#include<mpi.h>
#include<mutex>
#include<shared_mutex>
#include<iostream>
#pragma once

void probabilityIncreaseVectorSize(std::vector<int> *buffer, int generatedNumber);
void declareStatus(int* task_id, int status);
int local_reduction(std::vector<int> *buffer, float start_average, int start_treshold, int *window_buffer, MPI_Win *win, int rank, std::osyncstream &mainOut, std::osyncstream &senderOut, std::osyncstream &recieverOut);
void sendStatusFunction(std::vector<int> * buffer_size, float * local_average, int * threshold, std::atomic<bool> *done, std::shared_mutex *bufferMutex, std::osyncstream &senderOut, bool *sentFlag, std::shared_mutex *sentFlagMutex);
void recieveMessageFromMatchmaker(std::vector<int> *buffer, float * local_average, int * threshold, std::atomic<bool> *done, int *window_buffer, std::shared_mutex *bufferMutex, std::osyncstream &recieverOut, bool *sentFlag, std::shared_mutex *sentFlagMutex);
