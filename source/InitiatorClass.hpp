/**
 * @file InitiatorClass.hpp
 * @author Stefano Romeo
 * @brief Specialization file of initiator (root of the computational tree)
 * @version 0.1
 * @date 2024-04-03
 * 
 */

#include "mpi.h"
#include "MatchmakerClass.hpp"
#include "iostream"

using namespace std;

class InitiatorMatchmaker : public Matchmaker{

    MPI_Comm dataComm;                      ///<Communicator group of all reduction elements (root and worker nodes)
    int globalResult;                       ///<Final result of computation

    protected:
    /**
     * @brief Function to check termination of computation
     * If lower average is 0 causes all below nodes to recieve 0 average update and thus causing them to initiate termination procedures, only the initiator rank
     * can communicate instatly this value, all other matchmaker are prevented to send 0 value average to avoid early termination of reduction
     * 
     * @return 1 if lower average is 0
     */
        virtual int checkTermination(){
            if(lowerAverage == 0)
                return 1;

            return 0;
        }

        /**
         * @brief Ending procedure function to reduce all data
         * This function closes computation gathering all data from all workers, final values are recieved in order from every worker and the displayed 
         */
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

        /**
         * @brief This function updates average when metrics from the nodes below are recieved
         *
         * This version of the function is the only one that thruthfully communicates real value average to below nodes, to initialize termination this function
         * is able to set lower average value to 0 and the send this average value to all childs, this causes cascade termination 
         */
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
    /**
     * @brief Construct a new Initiator Matchmaker object
     *
     * The only initiator in the hierarchy is the root rank which creates a communicator group with all worker nodes, the constructor also inizializes globalResult variable
     * to zero (used for final reduction)
     * 
     * @param parentRank Rank of parent node (always -1 since this node is the root of the tree)
     * @param chunkSize Size of the chunk
     * @param recvBuffer Pointer to initial reciever buffer
     * @param totalParticles Number of total particles (in this case the total number of starting particles)
     * @param localAverage Local Average for root rank (total particles / number of childs)
     * @param localThreshold Local Threshold
     * @param childNumber Number of childs
     * @param childRanks Ranks of childs
     * @param dataComm Communicator containing all workers and root rank
     */
    InitiatorMatchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold,
                   int childNumber, int *childRanks, MPI_Comm dataComm) : Matchmaker(parentRank, chunkSize, recvBuffer, totalParticles, localAverage, 
                   localThreshold, childNumber, childRanks){
                    this->dataComm = dataComm;
                    globalResult = 0;
                   }

};