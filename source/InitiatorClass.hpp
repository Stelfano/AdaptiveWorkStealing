#include "mpi.h"
#include "MatchmakerClass.hpp"
#include "iostream"

using namespace std;

class InitiatorMatchmaker : public Matchmaker{

    MPI_Comm dataComm;
    int globalResult;

    protected:
        virtual int checkTermination(){
            if(lowerAverage == 0)
                return 1;

            return 0;
        }

        virtual void endingProcedure(){
            int dataCommSize = 0;
            MPI_Status stat;
            int localFlag;
            MPI_Comm_size(dataComm, &dataCommSize);

            for(int i = 0;i<dataCommSize-1;i++){
                MPI_Recv(&localFlag, 1, MPI_INT, MPI_ANY_SOURCE, DATA, dataComm, &stat);
                globalResult += localFlag;
            }

            cout << "TOTAL REDUCTION HAS ENDED WITH RESULT : " << globalResult << endl;
        }

        virtual void updateAverage(){
            float localSum = 0;

            for(int i = 0;i<childNumber;i++){
                localSum += valueArray[i];
            }

            //Attenzione potrebbe verificarsi una race condition
            lowerAverage = localSum/childNumber;

            totalParticles = localSum;
        }


    public:
    InitiatorMatchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold,
                   int childNumber, int *childRanks, MPI_Comm dataComm) : Matchmaker(parentRank, chunkSize, recvBuffer, totalParticles, localAverage, 
                   localThreshold, childNumber, childRanks){
                    this->dataComm = dataComm;
                    globalResult = 0;
                   }

};