// Diogo António Mesquita Ferrão da Silva 2024199827
// João Pedro Augusto Palmeiro	2023223918


#ifndef COMMON_H
#define COMMON_H

#include <semaphore.h>
#include <pthread.h>
#include <mqueue.h>
#include <time.h>
#include <ctype.h>

#define PIPE_NAME "input_pipe"
#define LOG_FILE "DEI_Emergency.log"
#define MQ_DOCTOR_NAME "/mq_doctor_prio"
#define SHM_NAME "/hospital_shm_data"
#define LOG_SIZE 10485760

#define HARD_LIMIT_QUEUE 100 
#define MAX_NAME 50

typedef struct {
    char nome[MAX_NAME];
    int tempo_triagem;
    int tempo_medico;
    int prioridade;
    int id;
    
    // Timestamps
    time_t arrival_time;    
    time_t end_triage_time; 
} Patient;

typedef struct {
    // --- ESTATÍSTICAS ---
    int total_atendidos;
    int total_triados;
    
    // CORREÇÃO: Adicionada a variável que faltava
    int total_tempo_medico;   

    long sum_wait_triage;     
    long sum_wait_doctor;     
    long sum_total_time;      
    
    // Contadores
    int pacientes_em_triagem;
    int pacientes_em_espera_med;
    int active_doctors;
    int active_triage;
    
    // Controlo
    time_t start_time;
    int running;

    // Fila
    Patient triage_queue[HARD_LIMIT_QUEUE]; 
    int t_in, t_out;
} HospitalData;

extern HospitalData *shm_data;

// Configuração
extern int conf_queue_max; 
extern int conf_shift;
extern int conf_msq_wait_max;

// Semáforos
extern sem_t sem_triage_fill;
extern sem_t sem_triage_empty;
extern sem_t sem_mutex_triage; 
extern sem_t sem_mutex_stats;

extern mqd_t mq_doctor;

// Funções
void write_log(char *msg);
void start_admission();
void* triage_worker(void* arg);
void doctor_process(int id, int is_temp);
void spawn_doctor(int id, int is_temp);
void spawn_triage(int id);

#endif