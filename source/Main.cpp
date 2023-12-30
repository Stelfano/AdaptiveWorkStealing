/**
 * @file Main.cpp
 * @author Stefano Romeo
 * @brief Uno scheduler adattivo per il work stealing
 * @version 0.1
 * @date 2023-11-22
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include <iostream>
#include <mpi.h>
#include "Worker.hpp"
#include "Matchmaker.hpp"
#include "utils.hpp"
#include <vector>
#include <cstdio>
#include <syncstream>
#include <cstring>

using namespace std;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

int main(int argc, char *args[]){

int provided;

auto mainOut = osyncstream{cout};
auto recieverOut = osyncstream{cout};
auto senderOut = osyncstream{cout};
freopen("log.txt", "w", stdout);

MPI_Init_thread(&argc, &args, MPI_THREAD_MULTIPLE, &provided);

int dim = 65536;
int num_proc;
int task_id;

MPI_Comm_size(MPI_COMM_WORLD, &num_proc);
MPI_Comm_rank(MPI_COMM_WORLD, &task_id);

MPI_Request req;
int* array = new int[dim];
int chunk_size = dim/(num_proc-1);
int* recv_buffer = new int[chunk_size];
int local_result = 0;
int global_result = 0;
int sendArray[num_proc];
int dispArray[num_proc];
bool sentFlag = false;
int *window_buffer = new int[MAX_STEAL];
MPI_Win win;

float local_average;
int threshold;

MPI_Win_create(window_buffer, sizeof(int) * MAX_STEAL , sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);
MPI_Barrier(MPI_COMM_WORLD);

memset(window_buffer, 0, MAX_STEAL * sizeof(int));

if(task_id == 0){

	calculate_time(mainOut);
	mainOut << "PROBLEM DIMENSION : " << dim << " WITH " << num_proc-1 << " WORKERS" << endl;
	calculate_time(mainOut);
	mainOut << "CHUNK SIZE : " << chunk_size << " PERCENTAGE : " << ((float)chunk_size/dim)*100 << "%" << endl;

	for(int i = 0;i < dim;i++)
		array[i] = 1;

	sendArray[0] = 0;
	
	//SendArray e dispArray servono a regolare quanti dati mandare ad ogni worker nella scatterv
	for(int i = 1;i<num_proc;i++){
		sendArray[i] = chunk_size;
		dispArray[i] = (i-1)*chunk_size;
		}
	}
	
	calculate_time(mainOut);
	mainOut << "PROCESSOR : " << task_id << " CALLING SCATTER..." << endl;
	MPI_Scatterv(array, sendArray, dispArray, MPI_INT, recv_buffer, chunk_size, MPI_INT, 0, MPI_COMM_WORLD);
	local_average = chunk_size;
	threshold = (chunk_size*10)/100;
	mainOut.emit();

	vector<int> *vector_buffer = new vector<int>(recv_buffer, recv_buffer + chunk_size);

	if(task_id == 0){
		matchmakerMainLoop(num_proc, &global_result, chunk_size, window_buffer, &win, mainOut);
	}else{
		calculate_time(mainOut);
		mainOut << "PROCESSOR : " << task_id << " BEGIN REDUCTION" << endl;
		local_result = local_reduction(vector_buffer, local_average, threshold, window_buffer, &win, task_id, mainOut, senderOut, recieverOut);
		calculate_time(mainOut);
		mainOut << "PROCESSOR : " << task_id << " COMPUTATION ENDED, GOING IDLE WITH RESULT : " << local_result << endl;
	}


	if(task_id != 0){
		MPI_Send(&local_result, 1, MPI_INT, 0, DATA, MPI_COMM_WORLD);
		calculate_time(mainOut);
		mainOut <<"PROCESSOR : " << task_id << " FINAL VALUE OF : " << local_result << " SENT!" << endl;
	}

	calculate_time(mainOut);
	mainOut << "PROCESSOR: " << task_id << " ARRIVED AT BARRIER" << endl;
	MPI_Barrier(MPI_COMM_WORLD);

	if(task_id == 0){
		calculate_time(mainOut);
		mainOut << "TOTAL REDUCTION ENDED WITH RESULT : " << global_result << endl;
	}

	calculate_time(mainOut);
	mainOut << "PROCESSOR : " << task_id << " CLOSING..." << endl;
	MPI_Win_free(&win);
	MPI_Finalize();
	return 0;
}