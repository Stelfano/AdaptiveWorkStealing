#include "utils.hpp"
#include "Matchmaker.hpp"
#include "mpi.h"
#include <iostream>

using namespace std;

/**
 * @brief Rappresenta il ciclo di operazione principale del Matchmaker
 *
 * Contiene al suo interno le funzioni di creazione del vettore di stato, di comunicazione verso gli altri rank, di modifica delle metriche
 * e di decisione del work stealing, questi elementi sono delegati a funzioni esterne ma sono richiamati qui, si occupa inoltre di decidere
 * quando la computazione dei nodi finisce ed inizia la riduzione
 * 
 * @param num_proc Indica il numero di nodi sottostanti nell'albero gerarchico della computazione
 * @param global_result Puntatore ad un elemento utilizzato nel main, conterrà il risultato finale della computazione
 * @param chunk_size Grandezza del vettore inviato ad ogni processo, serve ad inizializzare le metriche del work stealing
 */
void matchmakerMainLoop(int num_proc, int * global_result, int chunk_size){

	int tagArray[num_proc-1];      		    //Array contenente gli status dei singoli nodi
	int *valueArray = new int[num_proc-1];	//Array contenente le medie dei singoli nodi
	int local_flag = 0;						//Conterrà il valore della media locale del singolo nodo
	int terminated_processes = 0;			//Numero di processi che hanno terminato la loro computazione
	int threshold = 100;
	float local_average = chunk_size;	//Media locale del matchmaker, questa sarà quella veritiera ed aggiornata in modo lazy
	MPI_Status stat;					//Status della richiesta, ogni comunicazione con un nodo viene salvata qui in modo da poter accedere all
										//info di quello specifico nodo, regola chi ha mandato la richiesta e l'aggiornamento del suo status

	int last_victim = 0;
	int last_target = 0;


	//Inizializzazione dei valori da inviare ad ogni worker
	for(int i = 0;i<num_proc-1;i++){
		tagArray[i] = STABLE;
		valueArray[i] = chunk_size;
	}

	while(true){
		if(terminated_processes == num_proc-1)
			break;

		stat = waitControlMessages(&local_flag);	//Si riceve nella variabile local_flag il valore sulla media locale del singolo nodo
		if(stat.MPI_TAG == OVERWORK){
			calculate_time();
			cout << "CORE : " << stat.MPI_SOURCE << " IS OVERWORKED" << endl;
			tagArray[stat.MPI_SOURCE-1] = stat.MPI_TAG;		//Aggiorno il tag di status del nodo worker
			valueArray[stat.MPI_SOURCE-1] = local_flag;		//Aggiorno il valore di elementi del nodo worker
			updateAverage(&local_average, num_proc, valueArray);
			notifyAverage(&local_average, stat.MPI_SOURCE);
			int reciever = findPossibleReciever(tagArray, num_proc-1, stat.MPI_SOURCE-1);

			if(reciever != -1){
				checkForDoubleSteal(&threshold, &last_victim, &last_target, stat.MPI_SOURCE, reciever, num_proc);
				calculate_time();
				cout << "CORE : " << stat.MPI_SOURCE << " SHOULD LET ITS DATA BE STOLEN FROM : " << reciever << endl;
			}
		}

		if(stat.MPI_TAG == STABLE){
			calculate_time();
			cout << "CORE : " << stat.MPI_SOURCE << " IS STABLE" << endl;
			tagArray[stat.MPI_SOURCE-1] = stat.MPI_TAG;
			valueArray[stat.MPI_SOURCE-1] = local_flag;
			updateAverage(&local_average, num_proc, valueArray);
			notifyAverage(&local_average, stat.MPI_SOURCE);
		}

		if(stat.MPI_TAG == UNDERWORK){
			calculate_time();
			cout << "CORE : " << stat.MPI_SOURCE << " IS UNDERWORKED" << endl;
			tagArray[stat.MPI_SOURCE-1] = stat.MPI_TAG;
			valueArray[stat.MPI_SOURCE-1] = local_flag;
			updateAverage(&local_average, num_proc, valueArray);
			notifyAverage(&local_average, stat.MPI_SOURCE);
			int target = findPossibleTarget(tagArray, num_proc-1, stat.MPI_SOURCE-1);

			if(target != -1){
				checkForDoubleSteal(&threshold, &last_victim, &last_target, target, stat.MPI_SOURCE, num_proc);
				calculate_time();
				cout << "CORE : " << stat.MPI_SOURCE << " SHOULD STEAL FROM " << target << endl;
			}
		}

		if(stat.MPI_TAG == IDLE){
			calculate_time();
			cout << "CORE : " << stat.MPI_SOURCE << " IS IDLE!" << endl;
			tagArray[stat.MPI_SOURCE-1] = stat.MPI_TAG;
			valueArray[stat.MPI_SOURCE-1] = local_flag;

			threshold -= 50; 		//Penalità per core IDLE
			notifyThreshold(&threshold, num_proc);

			calculate_time();
			cout << "TRESHOLD DECREASES TO : " << threshold << endl;
			updateAverage(&local_average, num_proc-1, valueArray);
			notifyAverage(&local_average, stat.MPI_SOURCE);
			int target = findPossibleTarget(tagArray, num_proc-1, stat.MPI_SOURCE-1);

			if(target != -1){
				checkForDoubleSteal(&threshold, &last_victim, &last_target, target, stat.MPI_SOURCE, num_proc);
				calculate_time();
				cout << "CORE : " << stat.MPI_SOURCE << " SHOULD IMMEDIATLY STEAL FROM " << target << endl;
			}
		}

		if(stat.MPI_TAG == DATA){
			calculate_time();
			cout << "FINAL DATA RECIEVED FROM A PROCESS" << endl;
			*global_result += local_flag;
			terminated_processes++;
		}
	}		

	threshold = 0;
	notifyThreshold(&threshold, num_proc);
}

/**
 * @brief Attende la ricezione di un messaggio di controllo da uno dei nodi
 *
 *
 * @param recv_flag Contiene il numero di elementi da elaborare per ogni nodo, viene inviato dal worker al matchmaker.
 */

MPI_Status waitControlMessages(int *recv_flag){
	MPI_Status stat;

	MPI_Recv(recv_flag, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &stat);
	return stat;
}


/**
 * @brief Controlla la terminazione dei processi
 *
 *
 * @param tagArray Contiene i tag dei singoli processi, se tutti quanti hanno inviati i dati invia la terminazione
 * @param num_proc Numero dei processi attivi
 */
int checkTermination(int* tagArray, int num_proc){
	int flag = 1;
	
	for(int i = 0;i<num_proc;i++){
		if(tagArray[i] != DATA){
			flag = 0;
		}
	}

	return flag;
}

/**
 * @brief Trova una possibile vittima per il work stealing
 *
 * Questa funzione viene invocata non appena un core diventa Underworked, tentando di trovare immediatamente un bersaglio
 * per poter eseguire il work stealing, viene fatta una ricerca lineare all'interno del vettore degli stati, può essere 
 * sostituito con l'utilizzo di una coda di priorità con valori sentinella
 * 
 * @param tagArray Contiene il tag dei nodi worker
 * @param num_proc Contiene il numero dei processi attivi sotto il matchmaker
 * @param victim Contiene l'indice del ricevente per evitare che si generino errori
 * @return int Indice della vittima di work stealing
 */
int findPossibleTarget(int *tagArray, int num_proc, int victim){
	
	calculate_time();
	cout << "TAG ARRAY : ";
	for(int i = 0;i < num_proc; i++){
		cout << tagArray[i] << " ";
		if(tagArray[i] == OVERWORK && i != victim)
			return i+1;
	}
	cout << endl;

	return -1;
}

/**
 * @brief Trova una possibile ricevente per il work stealing
 *
 * Questa funzione viene invocata non appena un core diventa Overworked, tentando di trovare immediatamente un bersaglio
 * per poter eseguire il work stealing, viene fatta una ricerca lineare all'interno del vettore degli stati, può essere 
 * sostituito con l'utilizzo di una coda di priorità con valori sentinella
 * Viene prima data precedenza ai core IDLE che vengono cercati per prima all'interno dell'array
 * 
 * @param tagArray Contiene il tag dei nodi worker
 * @param num_proc Contiene il numero dei processi attivi sotto il matchmaker
 * @param victim Contiene l'indice della vittima per evitare che si generino errori
 * @return int Indice della ricevente di work stealing
 */
int findPossibleReciever(int* tagArray, int num_proc, int target){
	calculate_time();
	cout << "TAG ARRAY : ";
	for(int i = 0;i < num_proc; i++){
		cout << tagArray[i] << " ";
		if(tagArray[i] == IDLE && i != target)
			return i+1;
	}

	cout << endl;

	for(int i = 0;i < num_proc; i++){
		if(tagArray[i] == OVERWORK && i != target)
			return i+1;
	}

	return -1;
}

/**
 * @brief Aggiorna la media locale del matchmaker per il workstealing
 *
 * Aggiorna la media locale del matchmaker per tenere conto della situazione globale del sistema, la media reale verrà notificata
 * ai worker ogni volta che questi cambieranno stato (in modo lazy), questo metodo aggiorna e ricalcola solo la media locale
 * 
 * @param local_average Contiene il valore attuale di media del matchmaker
 * @param num_proc Contiene il numero di processi sttivi sotto il matchmaker
 * @param valueArray Contiene i valori di media forniti dai worker sottostanti
 */
void updateAverage(float * local_average, int num_proc, int * valueArray){
	int local_sum = 0;

	for(int i = 0;i<num_proc;i++){
		local_sum += valueArray[i];
	}

	*local_average = ((float)local_sum)/num_proc;
	calculate_time();
	cout << "MATCHMAKER DECLARES NEW AVERAGE AT : " << *local_average << endl;
}

/**
 * @brief Notifica il cambio di media ai nodi worker
 * 
 * @param local_average Valore della nuova media
 * @param reciever Ricevente del nuovo valore
 */
void notifyAverage(float * local_average, int reciever){
	MPI_Request req;
	
	calculate_time();
	cout << "MATCHMAKER UPDATING METRICS TO : " << reciever << endl;
	MPI_Send(local_average, 1, MPI_FLOAT, reciever, AVERAGE, MPI_COMM_WORLD);
}

void notifyThreshold(int * threshold, int num_proc){

	calculate_time();
	cout << "SENDING THRESHOLD UPDATE TO ALL WORKERS" << endl;
	
	for(int i = 0;i<num_proc-1;i++){
		MPI_Send(threshold, 1, MPI_INT, i+1, THRESHOLD, MPI_COMM_WORLD);
	}
}

/**
 * @brief Controlla se ci sono stati eventi di double stealing e aggiorna il threshold di conseguenza
 * 
 * Rileva un evento di double stealing e aggiorna il threshold di conseguenza, viene inoltre immediatamente iniziata la procedura
 * di notifica dell evento a tutti i worker sottostanti
 *
 * @param threshold Il valore attuale di threshold
 * @param last_victim Ultima vittima di workstealing
 * @param last_target Ultimo target di workstealing
 * @param current_victim Vittima dello scambio attuale
 * @param current_target Target dello scambio attuale
 */
void checkForDoubleSteal(int * threshold, int * last_victim, int * last_target, int current_victim, int current_target, int num_proc){
	if(current_victim == *last_victim && current_target == *last_target){
		*threshold += 5;
		calculate_time();
		cout << "NEW THRESHOLD SET T0 : " << *threshold << endl;
		notifyThreshold(threshold, num_proc);
	}

	*last_victim = current_victim;
	*last_target = current_target;
}