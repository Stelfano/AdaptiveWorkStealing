#include "mpi.h"
#include <iostream>
#include <random>
#include <vector>

using namespace std;


class Particle{
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
    start = MPI_Wtime();
    int taskId = 0;
    int taskNumber;
    int localResult;
    int globalResult;
    vector<int> buffer;
    Particle p;

    int totalParticles = 2000000/3;

    random_device randomDev;
    default_random_engine randomEng(randomDev());
    randomEng.seed(42);
    uniform_int_distribution<int> uniform_dist(0, 1);


    for(int i = 0;i< totalParticles;i++){
        p.moveParticle(10000);
    }
    
    if(taskId == 0)
        cout << "FINAL RESULT : " << globalResult << endl;

    end = MPI_Wtime();
    if(taskId == 0)
        cout << "TIME ELAPSED : " << end - start << endl;
    MPI_Finalize();
}