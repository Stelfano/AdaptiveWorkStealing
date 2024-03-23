class TerminalMatchmaker : public Matchmaker{

//Il terminal matchmaker è a diretto contatto con un worker

    protected:
         virtual int stealFromVictim(int *stealingQuantity, int victimRank){
            int actualSteal = 0;

            if(*stealingQuantity > MAX_STEAL){
                *stealingQuantity = MAX_STEAL;
            }
            memset(outWindowBuffer, 0, MAX_STEAL * sizeof(int));

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, victimRank, 0, outWindow);
            MPI_Get(outWindowBuffer, *stealingQuantity, MPI_INT, victimRank, sizeof(int), *stealingQuantity, MPI_INT, outWindow);
            MPI_Win_unlock(victimRank, outWindow);

            cout << "SAMPLE --> " << outWindowBuffer[0] << endl;

            for(int i = 0;i<*stealingQuantity;i++){
                if(outWindowBuffer[i] == 1 && actualSteal < *stealingQuantity){
                    actualSteal++;
                }
            }
            MPI_Send(&actualSteal, 1, MPI_INT, victimRank, VICTIM, MPI_COMM_WORLD);


            return actualSteal;
        }

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

        virtual int gatherData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity);
            int *tempArray = new int[stealingQuantity];
            int arrayOffset = 0;
            int actualSteal = 0;

            for(int i = 0;i<childNumber;i++){
                tagArray[i] = LOCKED;
                cout << "STEALING FROM WORKER : " << childRanks[i] << " " << stealingArray[i] << " PARTICLES" << endl;
                actualSteal = stealFromVictim(stealingArray+i, childRanks[i]);
                cout << "STOLEN : " << actualSteal << " FROM WORKER : " << (i+offset) << endl;
                memcpy(tempArray+arrayOffset, outWindowBuffer, actualSteal);
                arrayOffset += actualSteal;
            }

            memcpy(outWindowBuffer, tempArray, arrayOffset);

            return arrayOffset;
        }

    public:
        TerminalMatchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold,
                   int childNumber, int *childRanks) : Matchmaker(parentRank, chunkSize, recvBuffer, totalParticles, localAverage, localThreshold,
                   childNumber, childRanks){};
};