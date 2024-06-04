/**
 * @file Main.cpp
 * @author Stefano Romeo
 * @brief Adaptive work stealing scheduler for distributed computing
 * @version 0.1
 * @date 2023-11-22
 * 
 * 
 */

#include <iostream>
#include <mpi.h>
#include "WorkerClass.hpp"
#include "InitiatorClass.hpp"
#include "TerminalClass.hpp"
#include "utils.hpp"
#include <vector>
#include <cstdio>
#include <syncstream>
#include <cstring>
#include <unistd.h>
#include <string>

using namespace std;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

int main(int argc, char *args[]){

int provided;
double start, end;
//freopen("log.txt", "w", stdout);

if(argc < 2){
	cout << "ERROR, NO PROBLEM DIMENSION GIVEN" << endl;
	exit(1);
}

if(argc < 3){
	cout << "ERROR, NO THRESHOLD INITIAL VALUE GIVEN" << endl;
	exit(2)
}

MPI_Init_thread(&argc, &args, MPI_THREAD_MULTIPLE, &provided);

int problemDimension = stoi(args[1]);
int processNumber;
int taskId;

MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
MPI_Comm_rank(MPI_COMM_WORLD, &taskId);

if(taskId == 0)
	start = MPI_Wtime();

MPI_Group groupWorld;
MPI_Group dataGroup;
MPI_Comm dataComm;
int *array = new int[problemDimension];
int localResult = 0;
int globalResult = 0;
bool sentFlag = false;
Matchmaker *Match;
Worker *Work;
int treeWidth = 2;
int *childs = new int[treeWidth];

for(int i = 0;i<treeWidth;i++){
	childs[i] = -1;
}

int parentRank = 0;
int initialLeafRank = findInitialLeaf(processNumber-1, treeWidth);
int chunkSize = problemDimension/(processNumber - initialLeafRank);
int *recvBuffer = new int[chunkSize];
int leafProcesses[processNumber - initialLeafRank + 1];
int sendArray[processNumber - initialLeafRank + 1];
int dispArray[processNumber - initialLeafRank + 1];

parentRank = setPositionInTree(taskId, processNumber-1, treeWidth, childs);
int nodeLevel = findLevelInTree(taskId, treeWidth, processNumber-1);
float localAverage = chunkSize*pow(treeWidth, nodeLevel-1);
int thresholdValue = stoi(args[2]);
int threshold = (localAverage*thresholdValue)/100;

leafProcesses[0] = 0;

for(int i = 1; i < processNumber - initialLeafRank + 1;i++)
	leafProcesses[i] = initialLeafRank + i - 1;

MPI_Comm_group(MPI_COMM_WORLD, &groupWorld);
MPI_Group_incl(groupWorld, processNumber - initialLeafRank + 1, leafProcesses, &dataGroup);
MPI_Comm_create(MPI_COMM_WORLD, dataGroup, &dataComm);


if(taskId == 0){

	for(int i = 0;i < problemDimension;i++)
		array[i] = 1;

	dispArray[0] = 0;
	sendArray[0] = 0;
	for(int i = 1; i < processNumber - initialLeafRank + 1;i++){
		dispArray[i] = (i-1)*chunkSize;
		sendArray[i] = chunkSize;
	}

	calculate_time();
	cout << "I RANK DA : " << initialLeafRank << " A : " << processNumber - 1 << " SONO WORKERS" << endl;
	}

	if(childs[0] == -1){
		Work = new Worker(parentRank, recvBuffer, chunkSize, localAverage, threshold);
		
		MPI_Scatterv(array, sendArray, dispArray, MPI_INT, recvBuffer, chunkSize, MPI_INT, 0, dataComm);
		int temp = 0;
		for(int i = 0; i<chunkSize;i++){
			temp +=recvBuffer[i];
		}
		cout << "RECIEVED DATA : " << temp << endl << endl;
	}else{
		//Si assume per il momento un albero perfettamente bilanciato
		cout << "I AM A MATCHMAKER" << endl << endl;
		if(taskId == 0)
			Match = new InitiatorMatchmaker(parentRank, chunkSize*pow(treeWidth, nodeLevel-2), recvBuffer, chunkSize*pow(treeWidth, nodeLevel-1), localAverage, threshold, thresholdValue, treeWidth, childs, dataComm);
		else{
			if(nodeLevel-2 == 0)
				Match = new TerminalMatchmaker(parentRank, chunkSize*pow(treeWidth, nodeLevel-2), recvBuffer, chunkSize*pow(treeWidth, nodeLevel-1), localAverage, threshold, thresholdValue, treeWidth, childs);
			else
				Match = new Matchmaker(parentRank, chunkSize*pow(treeWidth, nodeLevel-2), recvBuffer, chunkSize*pow(treeWidth, nodeLevel-1), localAverage, threshold, thresholdValue, treeWidth, childs);
		}

		if(taskId == 0){
			MPI_Scatterv(array, sendArray, dispArray, MPI_INT, recvBuffer, chunkSize, MPI_INT, 0, dataComm);
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);

	
	if(childs[0] != -1){
        cout << "RANK : " << taskId << " INITIATING MAIN LOOP" << endl;
		Match->matchmakerMainLoop(&globalResult);
	}else{
		calculate_time();
		cout << "PROCESSOR : " << taskId << " BEGIN REDUCTION" << endl;
		localResult = Work->localReduction();
		calculate_time();
		cout << "PROCESSOR : " << taskId << " COMPUTATION ENDED, GOING IDLE WITH RESULT : " << localResult << endl;
	}


	if(childs[0] == -1){
		MPI_Send(&localResult, 1, MPI_INT, 0, DATA, dataComm);
		calculate_time();
		cout <<"PROCESSOR : " << taskId << " FINAL VALUE OF : " << localResult << " SENT!" << endl;
		delete Work;
		delete[] childs;
	}else{
		delete Match;
	}

	delete[] array;
	calculate_time();
	if(taskId == 0){
		end = MPI_Wtime();
		cout << "---PROCESS HAS ENDED IN " << end - start << " SECONDS---" << endl;
	}
	cout << "PROCESSOR : " << taskId << " CLOSING..." << endl;
	MPI_Finalize();
	return 0;
}