#include "Node.hpp"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <syncstream>
#include <unistd.h>

using namespace std;

class Worker : public Node{

    protected:
        vector<int> *buffer;
        int counter;
        
        void probabilityIncreaseVectorSize(int generatedNumber){

            if(totalParticles < abs(generatedNumber))
                return;
            
            if(generatedNumber > 0){
                for(int i = 0;i < generatedNumber;i++){
                    buffer->push_back(1);
                    totalParticles++;
                }
            }

            if(generatedNumber < 0){
                for(int i = 0;i < abs(generatedNumber);i++){
                    buffer->pop_back();
                    totalParticles--;
                }
            }
        }

        void injectDataInNode(int stealingQuantity){
            counter++;
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, nodeRank, 0, inWindow);
            cout << "WORKER : " << nodeRank << "DECLARES : " << totalParticles << " BEFORE STEALING" << endl;
            buffer->insert(buffer->end(), inWindowBuffer, inWindowBuffer + stealingQuantity);
            MPI_Win_unlock(nodeRank, inWindow);
            totalParticles += stealingQuantity;
            memset(inWindowBuffer, 0, MAX_STEAL);
            cout << "WORKER : " << nodeRank << " HAS RECIEVED : " << stealingQuantity << " PARTICLES" << endl;
            cout << "WORKER : " << nodeRank << " HAS INTEGRATED : " << totalParticles << " PARTICLES " << counter << endl;

            declareStatus(UNLOCKED);
        }

        void deleteDataFromNode(int stealingQuantity){
            int actualSteal = 0;
            if(totalParticles > stealingQuantity)
                actualSteal = stealingQuantity;
            else
                actualSteal = totalParticles;

            MPI_Send(&actualSteal, 1, MPI_INT, parentRank, COMM, MPI_COMM_WORLD);
            cout << "TOTAL PARTICLES AT DELETION TIME : " << buffer->size() << " IN NODE : " << nodeRank << endl;
            buffer->erase(buffer->begin(), buffer->begin() + actualSteal);
            totalParticles -= actualSteal;
            cout << "DELETED " << actualSteal << " PARTICLES FROM " << nodeRank << endl;
            cout << "BUFFER SIZE : " << buffer->size() << endl;
            declareStatus(UNLOCKED);
        }

    public:

        Worker(int parentRank, int *recvBuffer, int chunkSize, float localAverage, int threshold) : 
        Node(parentRank, chunkSize, recvBuffer, chunkSize, localAverage, threshold) {
               this->buffer = new vector<int>();

               totalParticles = chunkSize;
               counter = 0;
        }

        virtual ~Worker(){
            delete buffer;
        }


        int localReduction(osyncstream &mainOut, osyncstream &recieverOut, osyncstream &senderOut){

            random_device randomDev;
            thread statusThread;
            thread recieverThread;
            default_random_engine randomEng(randomDev());
            uniform_int_distribution<int> uniform_dist(0, 1);
            bool idleFlag = false;

            mainOut << "INITIAL SAMPLE --> " << recvBuffer[0] << endl;
            
            for(int i = 0;i<chunkSize;i++){
                buffer->push_back(recvBuffer[i]);
            }

            mainOut << "BUFFER SAMPLE --> " << buffer->size() << endl;
            mainOut.emit();


            //Qui si lanciano i thread
            statusThread = thread(&Worker::sendStatusFunction, this, ref(senderOut));
            recieverThread = thread(&Worker::recieveMessageFromMatchmaker, this, ref(recieverOut));

            int accumulatedResult = 0;
            int totalGenerated = 0;
            
            unique_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);

            while(!done){

                if(totalParticles > 0){
                    totalParticlesLock.lock();
                    accumulatedResult++;
                    int val = uniform_dist(randomEng);
                    totalGenerated+=val;
                    buffer->pop_back();
                    totalParticles--;

                    if(buffer->size() < MAX_STEAL){
                        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, nodeRank, 0, outWindow);
                        memset(outWindowBuffer, 0, MAX_STEAL);
                        copy(buffer->begin(),  buffer->end(), outWindowBuffer);	
                        MPI_Win_unlock(nodeRank, outWindow);
                    }else{
                        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, nodeRank, 0, outWindow);
                        copy(buffer->begin(), buffer->end(), outWindowBuffer);
                        MPI_Win_unlock(nodeRank, outWindow);
                    }

                    if(nodeRank == 5 || nodeRank == 6)
                        probabilityIncreaseVectorSize(val);

                    totalParticlesLock.unlock();
                }else{
                    totalParticles = 0;
                }
            }

            calculate_time(mainOut);
            mainOut << "LOCAL REDUCTION HAS ENDED IN RANK : " << nodeRank << endl;
            mainOut << "GENERATED PARTICLES : " << totalGenerated << endl;
            mainOut.emit();
            statusThread.join();
            recieverThread.join();
                
            return accumulatedResult;
        }
};