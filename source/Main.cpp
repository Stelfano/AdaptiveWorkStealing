//!!!IMPORTANTE ESEGUIRE SENZA CONNESSIONE ALLA RETE ALTRIMENTI BTL DARÀ PROBLEMI
//ERRORI VISIBILI CON mpirun -np 7 --mca btl_base-verbose 100 ./Test

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
#include "mpi.h"
#include "Worker.hpp"
#include "Matchmaker.hpp"
#include "utils.hpp"
#include <vector>
#include <cstdio>


#define OVERWORK 1
#define STABLE 2
#define UNDERWORK 3
#define IDLE 4
#define DATA 5


using namespace std;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

int main(int argc, char *args[]){

int provided;

freopen("log.txt", "w", stdout);

MPI_Init_thread(&argc, &args, MPI_THREAD_MULTIPLE, &provided);

int dim = 65532;
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

float local_average;
int threshold;

if(task_id == 0){

	calculate_time();		
	cout << "PROBLEM DIMENSION : " << dim << " WITH " << num_proc-1 << " WORKERS" << endl;
	calculate_time();
	cout << "CHUNK SIZE : " << chunk_size << " PERCENTAGE : " << ((float)chunk_size/dim)*100 << "%" << endl;
	for(int i = 0;i<dim;i++)
		array[i] = 1;

	sendArray[0] = 0;
	
	//SendArray e dispArray servono a regolare quanti dati mandare ad ogni worker nella scatterv
	for(int i = 1;i<num_proc;i++){
		sendArray[i] = chunk_size;
		dispArray[i] = (i-1)*chunk_size;
		}
	}
	
	calculate_time();
	cout << "PROCESSOR : " << task_id << " CALLING SCATTER..." << endl;
	MPI_Scatterv(array, sendArray, dispArray, MPI_INT, recv_buffer, chunk_size, MPI_INT, 0, MPI_COMM_WORLD);
	local_average = chunk_size;
	threshold = 100;

	if(task_id == 0){
		matchmakerMainLoop(num_proc, &global_result, chunk_size);
	}else{
		vector<int> vector_buffer(recv_buffer, recv_buffer + chunk_size);
		calculate_time();
		cout << "PROCESSOR : " << task_id << " BEGIN REDUCTION" << endl;
		local_result = local_reduction(&vector_buffer, local_average, threshold);
		calculate_time();
		cout << "PROCESSOR : " << task_id << " COMPUTATION ENDED, GOING IDLE WITH RESULT : " << local_result << endl;
	}

	if(task_id != 0){
		MPI_Send(&local_result, 1, MPI_INT, 0, DATA, MPI_COMM_WORLD);
		calculate_time();
		cout << "FINAL VALUE SENT!" << endl;
	}

	if(task_id == 0){
		calculate_time();
		cout << "PROCESS ENDED WITH RESULT : " << global_result << endl;
	}

	calculate_time();
	cout << "PROCESSOR : " << task_id << " CLOSING..." << endl;
	MPI_Finalize();
	return 0;
}