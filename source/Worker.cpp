#include "Worker.hpp"
#include "utils.hpp"
#include "mpi.h"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring>
#include <shared_mutex>
#include <mutex>
#include <syncstream>


using namespace std;

/**
 * @brief Aggiunge elementi al vettore 
 *
 * Funzione di aggiunta degli elementi al vettore della riduzione, se negativo verrano eliminati degli elementi
 * 
 * @param buffer Vettore di riduzione
 * @param generatedNumber Numero di elementi da aggiungere
 */
void probabilityIncreaseVectorSize(vector<int> *buffer, int generatedNumber){

	if(buffer->size() < abs(generatedNumber))
		return;
	
	if(generatedNumber > 0){
		for(int i = 0;i < generatedNumber;i++){
			buffer->push_back(1);
		}
	}

	if(generatedNumber < 0){
		for(int i = 0;i < abs(generatedNumber);i++){
			buffer->pop_back();
		}
	}
}

//Non c'è veramente bisogno di usare una funzione
void declareStatus(int *buffer_size, int status){
	MPI_Send(buffer_size, 1, MPI_INT, 0, status, MPI_COMM_WORLD);
}


/**
 * @brief Funzione di riduzione locale per il worker
 *
 * Questa funzione determina il comportamento dei worker, all'interno viene eseguito il main loop della riduzione e vengono lanciati i
 * thread che regolano la comunicazione dei worker con il matchmaker e lo scambio della metrica
 * 
 * @param buffer Buffer di contenimento per il valore finale
 * @param start_average Valore iniziale distribuito a tutti i core
 * @param start_treshold Valore di threshold iniziale
 * @return int Valore finale computato
 */
int local_reduction(vector<int> *buffer, float start_average, int start_treshold, int* window_buffer, MPI_Win *win, int rank, osyncstream &mainOut, osyncstream &recieverOut, osyncstream &senderOut){
	float local_average = start_average;
	int threshold = start_treshold;
	atomic<bool> done = false;

	calculate_time(mainOut);
	mainOut << "I AM RANK : " << rank << endl;
	calculate_time(mainOut);
	mainOut << "WORKER THRESHOLD BEGINS AT : " << start_treshold << endl;
	random_device random_dev;
	thread status_thread;
	thread reciever_thread;
	default_random_engine random_eng(random_dev());
	uniform_int_distribution<int> uniform_dist(0, 2);
	shared_mutex bufferMutex;
	shared_mutex sentFlagMutex;
	bool *sentFlag = new bool[4];
	memset(sentFlag, 0, sizeof(bool) * 4);


	//Qui si lanciano i thread
	status_thread = thread(sendStatusFunction, buffer, &local_average, &threshold, &done, &bufferMutex, ref(senderOut), sentFlag, &sentFlagMutex);
	reciever_thread = thread(recieveMessageFromMatchmaker, buffer, &local_average, &threshold, &done, window_buffer, &bufferMutex, ref(recieverOut), sentFlag, &sentFlagMutex);

	int accumulated_result = 0;
	
	calculate_time(mainOut);
	mainOut << "BUFFER SIZE : " << buffer->size() << endl;
	mainOut.emit();

	int buffer_size;
	shared_lock<shared_mutex> sharedBufferLock(bufferMutex, defer_lock);
	unique_lock<shared_mutex> uniqueBufferLock(bufferMutex, defer_lock);

//Attenzione a questa sezione, c'è il rischio di una race condition 
	while(!done){
		if(buffer->size() > 0){
			accumulated_result += buffer->back();
			int val = uniform_dist(random_eng);

			uniqueBufferLock.lock();
			buffer->pop_back();

			if(rank != 2){
				probabilityIncreaseVectorSize(buffer, val);
			}
			uniqueBufferLock.unlock();

			sharedBufferLock.lock();
			if(buffer->size() < MAX_STEAL){
				copy(buffer->begin(), buffer->begin()+buffer->size(), window_buffer);	
				memset(window_buffer + buffer->size(), 0, MAX_STEAL - buffer->size() + sizeof(int));
			}else{
				copy(buffer->begin(), buffer->begin()+buffer->size(), window_buffer);
			}
			sharedBufferLock.unlock();
		}
	}

	calculate_time(mainOut);
	mainOut << "LOCAL REDUCTION HAS ENDED!" << endl;
	mainOut.emit();
	status_thread.join();
	reciever_thread.join();
		
	return accumulated_result;
}

/**
 * @brief Funzione che regola la comunicazione Worker -> Matchmaker
 *
 * Questa funzione lanciata da un thread all'interno del mainloop del worker serve a notificare il matchmaker dei cambi di stato di un worker
 * e inviare valori sulla metrica locale del worker
 * 
 * @param buffer Vettore contenete i valori da ridurre, usiamo la funzione size per indicare il numero di elementi rimanenti
 * @param local_average Media locale da comunicare al matchmaker
 * @param threshold Threshold usato per indicare lo stato al matchmaker
 */
void sendStatusFunction(vector<int> * buffer, float * local_average, int * threshold, atomic<bool> *done, shared_mutex *bufferMutex, osyncstream &senderOut, bool *sentFlag, shared_mutex *sentFlagMutex){

	shared_lock<shared_mutex> sharedBufferLock(*bufferMutex, defer_lock);
	unique_lock<shared_mutex> uniqueFlagLock(*sentFlagMutex, defer_lock);

	while(!(*done)){	
		sharedBufferLock.lock();
		int buffer_size = buffer->size();
		sharedBufferLock.unlock();

		if(buffer_size > (*local_average + *threshold) && sentFlag[3] == false){
			declareStatus(&buffer_size, OVERWORK);
			uniqueFlagLock.lock();
			sentFlag[3] = true;

			sentFlag[2] = false;
			sentFlag[1] = false;
			sentFlag[0] = false;
			uniqueFlagLock.unlock();
		}

		if(buffer_size <= (*local_average + *threshold) && buffer_size >= (*local_average - *threshold) && sentFlag[2] == false){
			declareStatus(&buffer_size, STABLE);
			uniqueFlagLock.lock();
			sentFlag[2] = true;

			sentFlag[3] = false;
			sentFlag[1] = false;
			sentFlag[0] = false;
			uniqueFlagLock.unlock();
		}

		if(buffer_size < (*local_average - *threshold) && sentFlag[1] == false && buffer_size != 0){
			declareStatus(&buffer_size, UNDERWORK);
			uniqueFlagLock.lock();
			sentFlag[1] = true;

			sentFlag[3] = false;
			sentFlag[2] = false;
			sentFlag[0] = false;
			uniqueFlagLock.unlock();
		}

		if(buffer_size == 0 && sentFlag[0] == false){
			calculate_time(senderOut);
			senderOut << "I AM IDLE!" << endl;
			senderOut.emit();
			buffer_size = 0;
			declareStatus(&buffer_size, IDLE);
			uniqueFlagLock.lock();
			sentFlag[0] = true;

			sentFlag[3] = false;
			sentFlag[2] = false;
			sentFlag[1] = false;
			uniqueFlagLock.unlock();
		}
	}

	calculate_time(senderOut);
	senderOut << "SENDER THREAD TERMINATING" << endl;
}

//Funzione per la ricezione delle metriche da parte del matchmaker
void recieveMessageFromMatchmaker(vector<int> *buffer, float * local_average, int * threshold, atomic<bool> *done, int * window_buffer, shared_mutex *bufferMutex, osyncstream &recieverOut, bool *sentFlag, shared_mutex *sentFlagMutex){
	MPI_Status stat1, stat2, stat3, stat4;
	MPI_Request req1, req2, req3, req4;
	int flag1 = false;
	int flag2 = false;
	int flag3 = false;
	int flag4 = false;
	float bufferAvg = *local_average;
	int bufferThreshold = *threshold;
	int stealingBuffer = 0;

    unique_lock<shared_mutex> uniqueBufferLock(*bufferMutex, defer_lock);
    unique_lock<shared_mutex> uniqueFlagLock(*sentFlagMutex, defer_lock);

	calculate_time(recieverOut);
	recieverOut << "STARTING RECIEVER THREAD IN WORKER" << endl;
	recieverOut.emit();

	MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &req1);
	MPI_Irecv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &req2);
	MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, VICTIM, MPI_COMM_WORLD, &req3);
	MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, TARGET, MPI_COMM_WORLD, &req4);

	while(bufferAvg > 0){
		MPI_Test(&req1, &flag1, &stat1);
		MPI_Test(&req2, &flag2, &stat2);
		MPI_Test(&req3, &flag3, &stat3);
		MPI_Test(&req4, &flag4, &stat4);

		if(flag1 == true){
			MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &req1);
			*local_average = bufferAvg;
			flag1 = false;
		}

		if(flag2 == true){
			MPI_Irecv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &req2);
			*threshold = bufferThreshold;
			flag2 = false;
		}

		if(flag3 == true){
			MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, VICTIM, MPI_COMM_WORLD, &req3);
			calculate_time(recieverOut);
			recieverOut << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat3.MPI_TAG << " WITH VALUE : " << stealingBuffer << " IT'S STEALING TIME" << endl;
			calculate_time(recieverOut);
			recieverOut << "NUMBER OF ELEMENTS AT STEALING TIME : " << buffer->size() << endl;

			uniqueBufferLock.lock();
			buffer->erase(buffer->begin(), buffer->begin() + stealingBuffer);
			uniqueBufferLock.unlock();

			calculate_time(recieverOut);
			recieverOut << "VICTIM NEW BUFFER SIZE : " << buffer->size() << endl;

			calculate_time(recieverOut);
			int temp = buffer->size();
			
			declareStatus(&temp, UNLOCKED);

			uniqueFlagLock.lock();
			memset(sentFlag, 0, sizeof(bool) * 4);

			calculate_time(recieverOut);
			recieverOut << "FLAG VALUES : " << sentFlag[0] << " " << sentFlag[1] << " " << sentFlag[2] << " " << sentFlag[3] << endl;

			uniqueFlagLock.unlock();

			recieverOut.emit();
 
			flag3 = false;	
		}

		if(flag4 == true){
			MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, TARGET, MPI_COMM_WORLD, &req4);
			calculate_time(recieverOut);
			recieverOut << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat4.MPI_TAG << " WITH VALUE : " << stealingBuffer << " IT'S STEALING TIME" << endl;

			uniqueBufferLock.lock();
			buffer->insert(buffer->end(), window_buffer, window_buffer + stealingBuffer);
			uniqueBufferLock.unlock();
			calculate_time(recieverOut);
			recieverOut << "TARGET NEW BUFFER SIZE : " << buffer->size()  << endl;

			int temp = buffer->size();
			declareStatus(&temp, UNLOCKED);

			uniqueFlagLock.lock();
			memset(sentFlag, 0, sizeof(bool) * 4);

			calculate_time(recieverOut);
			recieverOut << "FLAG VALUES : " << sentFlag[0] << " " << sentFlag[1] << " " << sentFlag[2] << " " << sentFlag[3] << endl;

			uniqueFlagLock.unlock();

			recieverOut.emit();

			flag4 = false;	
		}

	}

	*done = true;
	MPI_Cancel(&req3);
	MPI_Cancel(&req4);
	calculate_time(recieverOut);
	recieverOut << "CLOSING RECIEVE THREAD" << endl;
	recieverOut.emit();
}
