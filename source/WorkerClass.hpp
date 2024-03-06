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
            buffer->insert(buffer->end(), inWindowBuffer, inWindowBuffer + stealingQuantity);
            totalParticles += stealingQuantity;
            declareStatus(UNLOCKED);
        }

        void deleteDataFromNode(int stealingQuantity){
            buffer->erase(buffer->begin(), buffer->begin() + stealingQuantity);
            totalParticles -= stealingQuantity;
            cout << "DELETED " << stealingQuantity << " PARTICLES FROM " << nodeRank << endl;
            declareStatus(UNLOCKED);
        }

    public:

        Worker(int parentRank, int *recvBuffer, int chunkSize, float localAverage, int threshold) : 
        Node(parentRank, chunkSize, recvBuffer, chunkSize, localAverage, threshold) {
               this->buffer = new vector<int>();

               for(int i = 0;i<chunkSize;i++){
                buffer->push_back(recvBuffer[i]);
               }
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
            totalParticles = buffer->size();
            bool idleFlag = false;
            


            //Qui si lanciano i thread
            statusThread = thread(&Worker::sendStatusFunction, this, ref(senderOut));
            recieverThread = thread(&Worker::recieveMessageFromMatchmaker, this, ref(recieverOut));

            int accumulatedResult = 0;
            
            unique_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);

        //Attenzione a questa sezione, c'è il rischio di una race condition 
            while(!done){

                if(totalParticles > 0){
                    totalParticlesLock.lock();
                    accumulatedResult++;
                    int val = uniform_dist(randomEng);
                    buffer->pop_back();
                    totalParticles--;

                    if(buffer->size() < MAX_STEAL){
                        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, nodeRank, 0, outWindow);
                        memset(outWindowBuffer, 0, MAX_STEAL * sizeof(int));
                        copy(buffer->begin(),  buffer->end(), outWindowBuffer);	
                        MPI_Win_unlock(nodeRank, outWindow);
                    }else{
                        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, nodeRank, 0, outWindow);
                        copy(buffer->begin(), buffer->end(), outWindowBuffer);
                        MPI_Win_unlock(nodeRank, outWindow);
                    }

                    probabilityIncreaseVectorSize(val);

                    totalParticlesLock.unlock();
                }
            }

            calculate_time(mainOut);
            mainOut << "LOCAL REDUCTION HAS ENDED IN RANK : " << nodeRank << endl;
            mainOut.emit();
            statusThread.join();
            recieverThread.join();
                
            return accumulatedResult;
        }

};