#include<vector>
#include<atomic>
#include<mpi.h>
#pragma once

void probabilityIncreaseVectorSize(std::vector<int> *buffer, int generatedNumber);
void declareStatus(int* task_id, int status);
int local_reduction(std::vector<int> *buffer, float start_average, int start_treshold, int *window_buffer, MPI_Win *win, int rank);
void sendStatusFunction(std::vector<int> * buffer_size, float * local_average, int * threshold, std::atomic<bool> *done);
void recieveMessageFromMatchmaker(std::vector<int> *buffer, float * local_average, int * threshold, std::atomic<bool> *done, int *window_buffer);
