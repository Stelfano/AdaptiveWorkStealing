/**
 * @file TerminalClass.hpp
 * @author Stefano Romeo
 * @brief Terminal Matchmaker definition
 * @version 0.1
 * @date 2024-04-03
 *
 * This file implements a Terminal Matchmaker, this is the last Matchmaker element before a worker, it reimplements work stealing related functions
 * to adapt them to the worker and the worker-matchmaker interaction instead of a matchmaker-matchmaker interaction
 */

class TerminalMatchmaker : public Matchmaker{

    protected:
    /**
     * @brief This function steals an amount of particles from a worker
     * 
     * During this function the stealing quantity is negotiated with the worker node and the window is then locked, after stealing has happend the actual
     * number of particles stolen is then returned, this function is also used in worker - matchmaker - worker interactions to move data from and to workers
     * in the same branch
     *
     * @param stealingQuantity Number of stolen particles
     * @param victimRank Rank of victim worker
     * @return Number of particle stolen (during reduction there could be remaining less particle that the number of intended particles to be stolen) 
     */
         virtual int stealFromVictim(int *stealingQuantity, int victimRank){
            int actualSteal = 0;

            if(*stealingQuantity > MAX_STEAL){
                *stealingQuantity = MAX_STEAL;
            }
            memset(outWindowBuffer, 0, MAX_STEAL * sizeof(int));

            MPI_Send(stealingQuantity, 1, MPI_INT, victimRank, VICTIM, MPI_COMM_WORLD);
            MPI_Recv(&actualSteal, 1, MPI_INT, victimRank, COMM, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, victimRank, 0, outWindow);
            MPI_Get(outWindowBuffer, actualSteal, MPI_INT, victimRank, sizeof(int), *stealingQuantity, MPI_INT, outWindow);
            MPI_Win_unlock(victimRank, outWindow);

            return actualSteal;
        }

        /**
         * @brief Stolen particles are sent to a worker
         * Particles are injected in the window buffer of a worker, the latter is then informed of the prensence of those particles
         * 
         * @param stealingQuantity Number of particles to inject in a worker
         * @param targetRank Rank of target
         */
        virtual void sendToTarget(int *stealingQuantity, int targetRank){

            if(*stealingQuantity > MAX_STEAL){
                *stealingQuantity = MAX_STEAL;
            }

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, targetRank, 0, inWindow);
            MPI_Put(inWindowBuffer, *stealingQuantity, MPI_INT, targetRank, sizeof(int), *stealingQuantity, MPI_INT, inWindow);

            MPI_Send(stealingQuantity, 1, MPI_INT, targetRank, TARGET, MPI_COMM_WORLD);
            memset(inWindowBuffer, 0, MAX_STEAL * sizeof(int));
            MPI_Win_unlock(targetRank, inWindow);

        }

        /**
         * @brief Function for worker - matchmaker - matchmaker interaction to push upwards stolen particles
         * This function gathers data from all child workers and pushes stolen particles upwards to another matchmaker, terminal - worker interaction
         * are the same as the single stealing functions, data are stolen in order and the procedure must be complete in order to steal from the next worker node
         * 
         * @param stealingQuantity Quantity to be stolen from all workers
         * @return Total number of particles stolen from all workers
         */
        virtual int gatherData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity);
            int *tempArray = new int[MAX_STEAL];
            int arrayOffset = 0;
            int actualSteal = 0;

            for(int i = 0;i<childNumber;i++){
                tagArray[i] = LOCKED;
                cout << "STEALING FROM WORKER : " << childRanks[i] << " " << stealingArray[i] << " PARTICLES" << endl;
                actualSteal = stealFromVictim(stealingArray+i, childRanks[i]);
                cout << "STOLEN : " << actualSteal << " FROM WORKER : " << (i+offset) << endl;
                //memcpy(tempArray+arrayOffset, outWindowBuffer, actualSteal);
                arrayOffset += actualSteal;
            }

            memcpy(outWindowBuffer+arrayOffset, tempArray, actualSteal);
            cout << "I HAVE STOLEN : " << arrayOffset << " PARTICLES " << endl;

            delete[] tempArray;
            delete[] stealingArray;
            return arrayOffset;
        }

    public:
        TerminalMatchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold,
                   int childNumber, int *childRanks) : Matchmaker(parentRank, chunkSize, recvBuffer, totalParticles, localAverage, localThreshold,
                   childNumber, childRanks){};
};