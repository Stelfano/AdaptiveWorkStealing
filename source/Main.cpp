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
#include "WorkerClass.hpp"
#include "MatchmakerClass.hpp"
#include "utils.hpp"
#include <vector>
#include <cstdio>
#include <syncstream>
#include <cstring>
#include <unistd.h>

using namespace std;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

int main(int argc, char *args[]){

int provided;

auto mainOut = osyncstream{cout};
auto recieverOut = osyncstream{cout};
auto senderOut = osyncstream{cout};
freopen("log.txt", "w", stdout);

MPI_Init_thread(&argc, &args, MPI_THREAD_MULTIPLE, &provided);

int problemDimension = 131072;
int processNumber;
int taskId;

MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
MPI_Comm_rank(MPI_COMM_WORLD, &taskId);

MPI_Request req;
int *array = new int[problemDimension];
int chunkSize = problemDimension/(processNumber-1);
int *recvBuffer = new int[chunkSize];
int localResult = 0;
int globalResult = 0;
int sendArray[processNumber];
int dispArray[processNumber];
bool sentFlag = false;
Matchmaker *Match;
Worker *Work;
int *childs = new int[4];

float localAverage = chunkSize;
int threshold = (chunkSize*10)/100;

MPI_Barrier(MPI_COMM_WORLD);

cout << "SURPASSED BARRIER" << endl;

if(taskId == 0){

	calculate_time(mainOut);
	mainOut << "PROBLEM DIMENSION : " << problemDimension << " WITH " << processNumber-1 << " WORKERS" << endl;
	calculate_time(mainOut);
	mainOut << "CHUNK SIZE : " << chunkSize << " PERCENTAGE : " << ((float)chunkSize/problemDimension)*100 << "%" << endl;
	mainOut.emit();

	for(int i = 0;i < problemDimension;i++)
		array[i] = 1;

	sendArray[0] = 0;
	
	//SendArray e dispArray servono a regolare quanti dati mandare ad ogni worker nella scatterv
	for(int i = 1;i<processNumber;i++){
		sendArray[i] = chunkSize;
		dispArray[i] = (i-1)*chunkSize;
		}


	childs[0] = 1;
	childs[1] = 2;
	childs[2] = 3;
	childs[3] = 4;

	}

	MPI_Scatterv(array, sendArray, dispArray, MPI_INT, recvBuffer, chunkSize, MPI_INT, 0, MPI_COMM_WORLD);

	if(taskId != 0){
		Work = new Worker(0, recvBuffer, chunkSize, chunkSize, localAverage, threshold);
	}else{
		Match = new Matchmaker(-1, chunkSize, recvBuffer, problemDimension, localAverage, threshold, 4, childs);
	}
	
	if(taskId == 0){
		Match->matchmakerMainLoop(&globalResult, mainOut);
	}else{
		calculate_time(mainOut);
		mainOut << "PROCESSOR : " << taskId << " BEGIN REDUCTION" << endl;
		localResult = Work->localReduction(mainOut, senderOut, recieverOut);
		calculate_time(mainOut);
		mainOut << "PROCESSOR : " << taskId << " COMPUTATION ENDED, GOING IDLE WITH RESULT : " << localResult << endl;
	}


	if(taskId != 0){
		MPI_Send(&localResult, 1, MPI_INT, 0, DATA, MPI_COMM_WORLD);
		calculate_time(mainOut);
		mainOut <<"PROCESSOR : " << taskId << " FINAL VALUE OF : " << localResult << " SENT!" << endl;
	}

	calculate_time(mainOut);
	mainOut << "PROCESSOR: " << taskId << " ARRIVED AT BARRIER" << endl;
	MPI_Barrier(MPI_COMM_WORLD);

	if(taskId == 0){
		calculate_time(mainOut);
		mainOut << "TOTAL REDUCTION ENDED WITH RESULT : " << globalResult << endl;
	}

	calculate_time(mainOut);
	mainOut << "PROCESSOR : " << taskId << " CLOSING..." << endl;
	MPI_Finalize();
	return 0;
}