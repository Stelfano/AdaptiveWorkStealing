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

using namespace std;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();

int main(int argc, char *args[]){

int provided;
double start, end;
//freopen("log.txt", "w", stdout);

MPI_Init_thread(&argc, &args, MPI_THREAD_MULTIPLE, &provided);

int problemDimension = 600000;
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
int *childs = new int[5];

for(int i = 0;i<5;i++){
	childs[i] = -1;
}

int levels = 3;
int *treeWidth = new int[3];
treeWidth[0] = 2;
treeWidth[1] = 3;
treeWidth[2] = 0;

int parentRank = 0;
int initialLeafRank = findInitialLeaf(treeWidth, levels);
int chunkSize = problemDimension/(processNumber - initialLeafRank + 1);
int *recvBuffer = new int[chunkSize];
int leafProcesses[processNumber - initialLeafRank + 1];
int sendArray[processNumber - initialLeafRank + 1];
int dispArray[processNumber - initialLeafRank + 1];

int levelNode = findLevelInTree(taskId, treeWidth, 8, 3);

int tw = 1;
for(int i = levelNode;i<levels-1;i++){
	tw *= treeWidth[i]; 
}

float localAverage = chunkSize*tw;
int thresholdValue = 10;
int threshold = (localAverage*thresholdValue)/100;

leafProcesses[0] = 0;

for(int i = 1; i < processNumber - initialLeafRank + 1;i++)
	leafProcesses[i] = initialLeafRank + i - 1;

MPI_Comm_group(MPI_COMM_WORLD, &groupWorld);
MPI_Group_incl(groupWorld, processNumber - initialLeafRank + 1, leafProcesses, &dataGroup);
MPI_Comm_create(MPI_COMM_WORLD, dataGroup, &dataComm);


if(taskId == 0){
	cout << "RANK : " << taskId << " DECLARES INTIAL LEAF AS : " << initialLeafRank << endl;
	cout << "CHUNK SIZE : " << chunkSize << endl;
}

cout << "RANK : " << taskId << " " << findLevelInTree(taskId, treeWidth, 8, 3) << endl;
parentRank = setPositionInTree(taskId, processNumber, treeWidth, childs, levels);
cout << "RANK : " << taskId << " WITH PARENT : " << parentRank << endl;

cout << "RANK : " << taskId << " DECLARES : " << localAverage << " STARTING AVERAGE" << endl;
cout << "RANK : " << taskId << " DECLARES : " << threshold << " STARTING THRESHOLD" << endl;

cout << "RANK : " << taskId << " HAS CHILDS : " << endl;
for(int i = 0;i<5;i++){
	cout << taskId << " : " << childs[i] << endl;
}

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
			Match = new InitiatorMatchmaker(parentRank, chunkSize, recvBuffer, problemDimension, localAverage, threshold, thresholdValue, treeWidth[levelNode], childs, dataComm);
		else{
			if(levelNode == levels - 1)
				Match = new TerminalMatchmaker(parentRank, chunkSize, recvBuffer, localAverage, localAverage, threshold, thresholdValue, treeWidth[levelNode], childs);
			else
				Match = new Matchmaker(parentRank, chunkSize, recvBuffer, localAverage, localAverage, threshold, thresholdValue, treeWidth[levelNode], childs);
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