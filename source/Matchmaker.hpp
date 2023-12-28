#include "mpi.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <syncstream>
#pragma once

void matchmakerMainLoop(int num_proc, int * global_result, int chunk_size, int *window_buffer, MPI_Win *win, std::osyncstream &mainOut);
MPI_Status waitControlMessages(int *recv_flag);
int checkTermination(int* tagArray, int num_proc);
int findPossibleTarget(int* tagArray, int num_proc, int victim);
int findPossibleVictim(int* tagArray, int num_proc, int target);
void updateAverage(float * local_average, int num_proc, int * valueArray);
void notifyAverage(float * local_average, int reciever);
void notifyThreshold(int * threshold, int num_proc);
void checkForDoubleSteal(int * treshold, int * last_victim, int * last_target, int current_victim, int current_target, int num_proc);
int setStealingQuantity(int target_index, int victim_index, int * valueArray, int local_average);
void printMetrics(int threshold, float local_average, int *valueArray, int num_proc, std::osyncstream &mainOut);
void stealFromVictim(int *window_buffer, int *stealing_quantity, MPI_Win * win, int victim_rank, int *tagArray);
void sendToTarget(int *window_buffer, int *stealing_quantity, MPI_Win *win, int target_rank, int *tagArray);