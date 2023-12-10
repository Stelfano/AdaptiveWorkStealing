#include "Worker.hpp"
#include "mpi.h"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>

/**
 * @def OVERWORK 
 * Segnala status di overworking, in questo stato un worker non può essere target di work stealing (non può ricevere altri dati), non appena questo status è segnalato viene
 * invocata una procedura per determinare un target a cui dare parte del lavoro del nodo sovraccarico
 */


/**
 * @def STABLE
 * Segnala status di stable, in questo stato un worker non può essere vittima o target di una procedura di work stealing, il nodo è interamente occupato ad
 * elaborare e computare i dati attuali
 * 
 */


/**
 * @def UNDERWORK
 * Segnala status di underwork, in questo stato un worker può essere target di work stealing, viene fatto ciò per evitare che il worker tocchi lo stato di 
 * IDLE che cerchiamo di evitare a tutti i costi per sfruttare al massimo il sistema
 * 
 */


/**
 * @def IDLE
 * Segnala status di Idle, in questo stato un worker deve essere target di work stealing al più presto per massimizzare le prestazioni, quando un core raggiunge
 * questo stato viene invocata la procedura del targeting e viene data priorità al targeting verso core Idle
 * 
 */


/**
 * @def IDLE
 * Segnala status di Idle, in questo stato un worker deve essere target di work stealing al più presto per massimizzare le prestazioni, quando un core raggiunge
 * questo stato viene invocata la procedura del targeting e viene data priorità al targeting verso core Idle
 * 
 */
/**
 * @def DATA
 * Tag utilizzato nei messaggi per segnalare al worker che i dati contenuti nel messaggio sono dati computazionali e non messaggi di stato sul sistema
 * 
 */
/**
 * @def METRICS
 * Tag utilizzato per indicare che il contenuto dei messaggi è un aggiornamento sulle metriche di sistema
 * 
 */
#define OVERWORK 1
#define STABLE 2
#define UNDERWORK 3
#define IDLE 4
#define DATA 5
#define AVERAGE 6
#define THRESHOLD 7

using namespace std;

// SUPPORT FUNCTIONS FOR WORKERS
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
int local_reduction(vector<int> *buffer, float start_average, int start_treshold){
	bool sentFlag = false;
	float local_average = start_average;
	int threshold = start_treshold;

	random_device random_dev;
	thread status_thread;
	thread reciever_thread;
	default_random_engine random_eng(random_dev());
	uniform_int_distribution<int> uniform_dist(0, 2);

	//Qui si lanciano i thread
	status_thread = thread(sendStatusFunction, buffer, &local_average, &threshold);
	reciever_thread = thread(recieveMessageFromMatchmaker, &local_average, &threshold);

	int accumulated_result = 0;
	cout << "BUFFER SIZE : " << buffer->size() << endl;

	int buffer_size;

	while(buffer->size() != 0){
		accumulated_result += buffer->back();
		buffer->pop_back();

		int val = uniform_dist(random_eng);
		probabilityIncreaseVectorSize(buffer, val);
	}

	local_average = 0;
	threshold = 0;
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
void sendStatusFunction(vector<int> * buffer, float * local_average, int * threshold){
	int buffer_size = buffer->size();
	bool sentFlag = false;

	while(*local_average != 0 && *threshold != 0){	
		buffer_size = buffer->size();

		if(buffer->size() > (*local_average - *threshold) && sentFlag == false){
			declareStatus(&buffer_size, OVERWORK);
			sentFlag = true;
		}

		if(buffer->size() < (*local_average + *threshold) && buffer->size() > (*local_average - *threshold) && sentFlag == true){
			declareStatus(&buffer_size, STABLE);
			sentFlag = false;
		}

		if(buffer->size() < (*local_average - *threshold) && sentFlag == false){
			declareStatus(&buffer_size, UNDERWORK);
			sentFlag = true;
		}

		if(buffer->size() == 0 && sentFlag == true){
			declareStatus(&buffer_size, IDLE);
			sentFlag = false;
		}
	}
}

//Funzione per la ricezione delle metriche da parte del matchmaker
void recieveMessageFromMatchmaker(float * local_average, int * threshold){
	MPI_Request request;
	MPI_Status stat1;
	MPI_Status stat2;
	int flag = false;
	float bufferAvg;
	int bufferThreshold;

	cout << "STARTING RECIEVER THREAD IN WORKER" << endl;

	MPI_Recv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &stat1);
	MPI_Recv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &stat2);

	while(bufferAvg != 0 && bufferThreshold != 0){

		*local_average = bufferAvg;

 		*threshold = bufferThreshold;
		

		cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat1.MPI_TAG << " WITH VALUE : " << bufferAvg << endl;
		cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat2.MPI_TAG << " WITH VALUE : " << bufferThreshold << endl;
		cout << "WORKER DECLARES THRESHOLD AT : " << *threshold << endl;
		cout << "WORKER DECLARES AVERAGE AT : " << *local_average << endl;
		//Successivamente aggiungere il caso sul work stealing
		MPI_Recv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &stat1);
		MPI_Recv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &stat2);


		flag = false;
	}

	cout << "CLOSING RECIEVE THREAD" << endl;
}