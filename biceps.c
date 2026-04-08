#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "creme.h"

/*
   Variables globales demandées dans le sujet.
   Mots : tableau contenant les mots de la commande
   NMots : nombre total de mots
*/
static char **Mots = NULL;
static int NMots = 0;

#define NBMAXC 20

typedef int (*TypeFonctionCommande)(int, char **);

typedef struct {
    char *nom;
    TypeFonctionCommande fonction;
} CommandeInterne;

static CommandeInterne TabComInt[NBMAXC];
static int NbComInt = 0;

char *copyString(char *s)
{
    char *copie;

    if (s == NULL) {
        return NULL;
    }

    copie = malloc(strlen(s) + 1);
    if (copie == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(copie, s);

    return copie;
}

void freeMots(void)
{
    int i;

    if (Mots != NULL) {
        for (i = 0; i < NMots; i++) {
            free(Mots[i]);
        }
        free(Mots);
    }

    Mots = NULL;
    NMots = 0;
}

int analyseCom(char *b)
{
    char *copie;
    char *courant;
    char *mot;
    char **tmp;

    freeMots();

    if (b == NULL) {
        return 0;
    }

    copie = copyString(b);
    courant = copie;

    while ((mot = strsep(&courant, " \t\n")) != NULL) {
        if (*mot == '\0') {
            continue;
        }

        tmp = realloc(Mots, (NMots + 2) * sizeof(char *));
        if (tmp == NULL) {
            perror("realloc");
            free(copie);
            freeMots();
            exit(EXIT_FAILURE);
        }

        Mots = tmp;
        Mots[NMots] = copyString(mot);
        NMots++;
        Mots[NMots] = NULL;
    }

    free(copie);
    return NMots;
}

void ajouteCom(char *nom, TypeFonctionCommande f)
{
    if (NbComInt >= NBMAXC) {
        fprintf(stderr, "Erreur : tableau des commandes internes plein\n");
        exit(EXIT_FAILURE);
    }

    TabComInt[NbComInt].nom = nom;
    TabComInt[NbComInt].fonction = f;
    NbComInt++;
}

int Sortie(int N, char *P[])
{
    (void)N;
    (void)P;

    /*
       Si le serveur BEUIP tourne encore, on l'arrete avant de quitter.
    */
    if (creme_get_server_pid() > 0) {
        creme_beuip_stop();
    }

    printf("Fin du programme biceps\n");
    freeMots();
    exit(0);
}

int ChangeRep(int N, char *P[])
{
    char *dest;

    if (N < 2) {
        dest = getenv("HOME");
        if (dest == NULL) {
            fprintf(stderr, "cd : HOME non defini\n");
            return 1;
        }
    } else {
        dest = P[1];
    }

    if (chdir(dest) != 0) {
        perror("cd");
        return 1;
    }

    return 0;
}

int AfficheRep(int N, char *P[])
{
    char chemin[1024];

    (void)N;
    (void)P;

    if (getcwd(chemin, sizeof(chemin)) == NULL) {
        perror("getcwd");
        return 1;
    }

    printf("%s\n", chemin);
    return 0;
}

int Version(int N, char *P[])
{
    (void)N;
    (void)P;

    printf("biceps version 2.0 - creme %s\n", CREME_VERSION);
    return 0;
}

/*
   Commande interne :
   beuip start pseudo
   beuip stop
*/
int CmdBeuip(int N, char *P[])
{
    if (N < 2) {
        fprintf(stderr, "Utilisation : beuip start pseudo | beuip stop\n");
        return 1;
    }

    if (strcmp(P[1], "start") == 0) {
        if (N != 3) {
            fprintf(stderr, "Utilisation : beuip start pseudo\n");
            return 1;
        }
        return creme_beuip_start(P[2]);
    }

    if (strcmp(P[1], "stop") == 0) {
        return creme_beuip_stop();
    }

    fprintf(stderr, "Commande beuip inconnue\n");
    return 1;
}

/*
   Commande interne :
   mess liste
   mess msg pseudo message
   mess all message
*/
int CmdMess(int N, char *P[])
{
    if (N < 2) {
        fprintf(stderr, "Utilisation : mess liste | mess msg pseudo message | mess all message\n");
        return 1;
    }

    if (strcmp(P[1], "liste") == 0) {
        return creme_mess_liste();
    }

    if (strcmp(P[1], "msg") == 0) {
        if (N != 4) {
            fprintf(stderr, "Utilisation : mess msg pseudo message\n");
            return 1;
        }
        return creme_mess_msg(P[2], P[3]);
    }

    if (strcmp(P[1], "all") == 0) {
        if (N != 3) {
            fprintf(stderr, "Utilisation : mess all message\n");
            return 1;
        }
        return creme_mess_all(P[2]);
    }

    fprintf(stderr, "Commande mess inconnue\n");
    return 1;
}

void majComInt(void)
{
    ajouteCom("exit", Sortie);
    ajouteCom("cd", ChangeRep);
    ajouteCom("pwd", AfficheRep);
    ajouteCom("vers", Version);
    ajouteCom("beuip", CmdBeuip);
    ajouteCom("mess", CmdMess);
}

void listeComInt(void)
{
    int i;

    printf("Commandes internes disponibles :\n");
    for (i = 0; i < NbComInt; i++) {
        printf(" - %s\n", TabComInt[i].nom);
    }
}

int execComInt(int N, char **P)
{
    int i;

    if (N <= 0 || P == NULL || P[0] == NULL) {
        return 0;
    }

    for (i = 0; i < NbComInt; i++) {
        if (strcmp(P[0], TabComInt[i].nom) == 0) {
            TabComInt[i].fonction(N, P);
            return 1;
        }
    }

    return 0;
}

int execComExt(char **P)
{
    pid_t pid;
    int status;

    if (P == NULL || P[0] == NULL) {
        return 0;
    }

#ifdef TRACE
    printf("[TRACE] Commande externe detectee : %s\n", P[0]);
#endif

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        execvp(P[0], P);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            return -1;
        }
    }

    return 0;
}

char *buildPrompt(void)
{
    char hostname[256];
    char *user;
    char fin;
    char *prompt;
    size_t taille;

    user = getenv("USER");
    if (user == NULL) {
        user = "unknown";
    }

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "machine");
    }
    hostname[sizeof(hostname) - 1] = '\0';

    fin = (geteuid() == 0) ? '#' : '$';

    taille = strlen(user) + strlen(hostname) + 4;

    prompt = malloc(taille);
    if (prompt == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    snprintf(prompt, taille, "%s@%s%c ", user, hostname, fin);

    return prompt;
}

int main(void)
{
    char *ligne;
    char *prompt;

    majComInt();
    listeComInt();

    while (1) {
        prompt = buildPrompt();
        ligne = readline(prompt);
        free(prompt);

        if (ligne == NULL) {
            printf("\n");
            Sortie(0, NULL);
        }

        analyseCom(ligne);

#ifdef TRACE
        printf("[TRACE] Nombre de mots : %d\n", NMots);
#endif

        if (!execComInt(NMots, Mots)) {
            execComExt(Mots);
        }

        free(ligne);
    }

    freeMots();
    return 0;
}
