**Arquitetura Base**
* A **Admissão** (processo pai) gere o sistema central, recebendo dados de um sensor externo através de um *Named Pipe* (`input_pipe`) e utilizando a chamada `select()` para multiplexagem de eventos.
* A **Triagem** opera através de uma *pool* dinâmica de *threads* (T1, T2...) que acedem partilhadamente ao *buffer* circular da fila de admissão.
* O **Atendimento Médico** utiliza processos independentes gerados por `fork()` para isolar falhas de execução, impedindo que erros locais (como *segmentation faults*) comprometam o processo principal.

**Comunicação (IPC) e Controlo de Concorrência**
* O estado global e as estatísticas assentam em **Memória Partilhada** (SHM) protegida rigorosamente por semáforos POSIX binários, de forma a prevenir *race conditions*.
* O encaminhamento seguro entre a triagem e os médicos é efetuado por uma **Fila de Mensagens** (`mq_doctor_prio`), que usufrui da ordenação nativa do sistema operativo para priorizar doentes urgentes.
* A gestão de entrada na fila circular recorre a políticas não-bloqueantes (`sem_trywait`) com dois semáforos de controlo, prevenindo o bloqueio do sistema quando a fila se encontra cheia.

**Resiliência e Tolerância a Falhas**
* O sistema implementa *Memory Mapped Files* (MMF) para otimizar o registo de eventos no ficheiro de *log*, delegando a sincronização com o disco ao próprio *kernel*.
* O processo principal monitoriza ativamente a morte dos processos médicos, gerindo substituições automáticas de turnos e lançando reforços temporários durante picos de carga de forma dinâmica.
* A interrupção do sistema (via SIGINT) desencadeia um protocolo de encerramento gracioso que aguarda a conclusão dos atendimentos em curso e liberta todos os recursos adequadamente, prevenindo a existência de processos *zombie*.

A construção técnica desta simulação utilizou intensivamente normas POSIX e foi concluída com mais de 50 horas de trabalho investido.
