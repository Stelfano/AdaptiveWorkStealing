#include "mpi.h"
#include <iostream>
#include <random>
#include <vector>

using namespace std;


class particle{
    public:
        void moveParticle(int workload) volatile{
            volatile int val = 0;

            for(int i = 0;i<workload;i++){
                    val++;
                }
        }
};


int main(int argc, char *argv[]){
    double start, end;
    MPI_Init(&argc, &argv);
    start = MPI_Wtime();
    int taskId = 0;
    int taskNumber;
    int localResult;
    int globalResult;
    vector<int> buffer;
    particle *p = new particle;

    MPI_Comm_rank(MPI_COMM_WORLD, &taskId);
    MPI_Comm_size(MPI_COMM_WORLD, &taskNumber);

    int problemDimension = 400000;
    int chunkSize = problemDimension/taskNumber;

    int *array = new int[chunkSize];
    int *recvBuffer = new int[chunkSize];
    int totalParticles = chunkSize;

    if(taskId == 0){
        for(int i = 0;i<chunkSize;i++){
            array[i] = 1;
        }
    }

    cout << "BEFORE SCATTER" << endl;
    MPI_Scatter(array, chunkSize, MPI_INT, recvBuffer, chunkSize, MPI_INT, 0, MPI_COMM_WORLD);

    random_device randomDev;
    default_random_engine randomEng(randomDev());
    randomEng.seed(42);
    uniform_int_distribution<int> uniform_dist(0, 1);

    for(int i=0;i<chunkSize;i++){
        buffer.push_back(recvBuffer[i]);
    }

    while(totalParticles > 0){
        localResult++;
        int val = uniform_dist(randomEng);
        buffer.pop_back();
        totalParticles--;

        p->moveParticle(10000);

        for(int i = 0;i<val;i++){
            buffer.push_back(1);
            totalParticles++;
        }
    }
    

    MPI_Reduce(&localResult, &globalResult, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if(taskId == 0)
        cout << "FINAL RESULT : " << globalResult << endl;

    end = MPI_Wtime();
    if(taskId == 0)
        cout << "TIME ELAPSED : " << end - start << endl;
    MPI_Finalize();

    delete p;
}