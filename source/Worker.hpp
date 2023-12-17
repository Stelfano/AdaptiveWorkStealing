#include<vector>
#include<atomic>
#pragma once

void probabilityIncreaseVectorSize(std::vector<int> *buffer, int generatedNumber);
void declareStatus(int* task_id, int status);
int local_reduction(std::vector<int> *buffer, float start_average, int start_treshold);
void sendStatusFunction(std::vector<int> * buffer_size, float * local_average, int * threshold, std::atomic<bool> *done);
void recieveMessageFromMatchmaker(float * local_average, int * threshold, std::atomic<bool> *done);