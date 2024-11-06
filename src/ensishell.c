/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h> // Pour execvp et fork
#include <sys/wait.h> // Pour waitpid et wait
#include <fcntl.h> //Pour la manipulation de mes fichiers au niveau de la question 6

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

// ========================================================================================
// QUESTION 1 : Lancement d'une commande
// QUESTION 2 : Lancement tache de fond ( Attente de terminaison)
// QUESTION 3 : Lancement tache de fond 
// QUESTION 4 : Lister les processus en tâches de fond
// QUESTION 5 : Pipe

#define MAX_JOBS 100

typedef struct {
    pid_t pid;
    char *command;
} Job;


Job jobs[MAX_JOBS];
int job_count = 0;


// Ajoute un nouveau processus en tâche de fond à la liste des jobs.
void ajouter_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        jobs[job_count].command = strdup(command);  // Allouer une nouvelle chaîne pour la commande
        job_count++;
    } else {
        printf("Erreur : trop de tâches en arrière-plan.\n");
    }
}

// Parcourt tous les processus en tâche de fond et retire de la liste des jobs et sa mémoire est libérée si un processus est terminé (result != 0),
void verifier_jobs() {
    for (int i = 0; i < job_count; i++) {
        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        if (result != 0) {
            // Le processus est terminé, on le retire de la liste
            printf("Processus %d terminé.\n", jobs[i].pid);
            free(jobs[i].command);
            // Retirer le job en décalant les éléments
            for (int j = i; j < job_count - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            job_count--;
            i--;  // On vérifie le même indice à nouveau car la liste a été décalée
        }
    }
}


// Affiche la liste des processus en tâche de fond.
void lister_jobs() {
    printf("Liste des processus en tâche de fond :\n");
    for (int i = 0; i < job_count; i++) {
        printf("PID: %d, Commande: %s\n", jobs[i].pid, jobs[i].command);
    }
}

// Exécute la commande fournie dans le shell
void executer_command(struct cmdline *l) {
    if (l == NULL || l->seq == NULL || l->seq[0] == NULL) {
        return;  // Pas de commande à exécuter
    }

	char **cmd = l->seq[0]; // Première commande dans la séquence
    pid_t pid = fork();

    if (pid == -1) {
        // Erreur lors du fork
        perror("fork");
        exit(EXIT_FAILURE);

    } 
	
	if (pid == 0) {
        // Processus enfant : exécuter la commande

        execvp(cmd[0], cmd);
        // Si execvp échoue
        perror("execvp");
        exit(EXIT_FAILURE);

    } else {
        // Processus père : attendre si nécessaire
        if (l->bg) { 
			ajouter_job(pid, cmd[0]);
			printf("[Processus en tâche de fond lancé: PID %d]\n", pid);
            
        } else {
			// Si la commande est en avant-plan, on attend sa fin
			waitpid(pid, NULL, 0);
        }
    }
}



// ========================================================================================

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */

	// Parse la ligne de commande
    struct cmdline *cmd = parsecmd(&line);
    if (cmd == NULL || cmd->seq[0] == NULL) {
        free(line);
        return -1; // Rien à exécuter
    }

    // Créer un processus fils
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        free(line);
        return -1;
    }

	/* Remove this line when using parsecmd as it will free it */
	free(line);
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	while (1) {
		struct cmdline *l;
		char *line=0;
		int i, j;
		char *prompt = "ensishell>";

		//************** Vérifier les processus en tâche de fond **************
        verifier_jobs();

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

		//*********** Vérifier si la commande est 'jobs' ***************
        if (strcmp(line, "jobs") == 0) {
            lister_jobs();  // Appeler la fonction qui liste les jobs
            free(line);
            continue;  // Retourner au prompt
        }

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
                        continue;
                }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
		  
			terminate(0);
		}
		

		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		/* Display each command of the pipe */
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
                        for (j=0; cmd[j]!=0; j++) {
                                printf("'%s' ", cmd[j]);
                        }
			printf("\n");
		}

		//============================================================================================
		// Execution de la commande 
		executer_command(l);
	}

	return 0;

}
