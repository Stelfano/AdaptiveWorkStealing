#include <chrono>
#include <mpi.h>
#include <ctime>
#include <iostream>
#pragma once
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
#define VICTIM 8
#define TARGET 9
#define LOCKED 10
#define UNLOCKED 11
#define MAX_STEAL 50000

extern std::chrono::time_point<std::chrono::system_clock> start;

void calculate_time();