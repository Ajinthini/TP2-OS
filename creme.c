#include "creme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define PORT 9998
#define LBUF 512
#define MAXCOUPLES 255
#define BROADCAST_IP "192.168.88.255"

typedef struct {
    unsigned long ip;
    char pseudo[256];
} Couple;

/*
   Etat interne de la librairie.
   On garde le pid du serveur fils pour pouvoir l'arreter.
*/
static pid_t serveur_pid = -1;

/*
   Fonctions internes a la librairie :
   elles sont static pour ne pas etre exportees
   inutilement dans la table des symboles.
*/
static char *addrip(unsigned long A)
{
    static char b[16];

    sprintf(b, "%u.%u.%u.%u",
            (unsigned int)(A >> 24 & 0xFF),
            (unsigned int)(A >> 16 & 0xFF),
            (unsigned int)(A >> 8 & 0xFF),
            (unsigned int)(A & 0xFF));

    return b;
}

static void construit_message_simple(char *dest, char code, const char *donnees)
{
    dest[0] = code;
    strcpy(dest + 1, "BEUIP");

    if (donnees != NULL) {
        strcpy(dest + 6, donnees);
    } else {
        dest[6] = '\0';
    }
}

static int construit_message_prive(char *dest, const char *pseudo, const char *message)
{
    dest[0] = '4';
    strcpy(dest + 1, "BEUIP");
    strcpy(dest + 6, pseudo);
    strcpy(dest + 6 + strlen(pseudo) + 1, message);

    return 6 + strlen(pseudo) + 1 + strlen(message);
}

static int message_valide(char *msg, int n)
{
    if (n < 6) {
        return 0;
    }

    if (strncmp(msg + 1, "BEUIP", 5) != 0) {
        return 0;
    }

    return 1;
}

static int deja_present(Couple *table, int nCouples, unsigned long ip, char *pseudo)
{
    int i;

    for (i = 0; i < nCouples; i++) {
        if (table[i].ip == ip && strcmp(table[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }

    return 0;
}

static void ajoute_couple(Couple *table, int *nCouples, unsigned long ip, char *pseudo)
{
    if (pseudo == NULL || *pseudo == '\0') {
        return;
    }

    if (deja_present(table, *nCouples, ip, pseudo)) {
#ifdef TRACE
        printf("[TRACE] Couple deja connu : %s - %s\n", addrip(ip), pseudo);
#endif
        return;
    }

    if (*nCouples >= MAXCOUPLES) {
        fprintf(stderr, "Table des couples pleine\n");
        return;
    }

    table[*nCouples].ip = ip;
    strncpy(table[*nCouples].pseudo, pseudo, sizeof(table[*nCouples].pseudo) - 1);
    table[*nCouples].pseudo[sizeof(table[*nCouples].pseudo) - 1] = '\0';
    (*nCouples)++;

#ifdef TRACE
    printf("[TRACE] Ajout couple : %s - %s\n", addrip(ip), pseudo);
#endif
}

static void supprime_couple_par_ip(Couple *table, int *nCouples, unsigned long ip)
{
    int i, j;

    for (i = 0; i < *nCouples; i++) {
        if (table[i].ip == ip) {
            for (j = i; j < *nCouples - 1; j++) {
                table[j] = table[j + 1];
            }
            (*nCouples)--;
            return;
        }
    }
}

static char *cherche_pseudo_par_ip(Couple *table, int nCouples, unsigned long ip)
{
    int i;

    for (i = 0; i < nCouples; i++) {
        if (table[i].ip == ip) {
            return table[i].pseudo;
        }
    }

    return NULL;
}

static unsigned long cherche_ip_par_pseudo(Couple *table, int nCouples, char *pseudo)
{
    int i;

    for (i = 0; i < nCouples; i++) {
        if (strcmp(table[i].pseudo, pseudo) == 0) {
            return table[i].ip;
        }
    }

    return 0;
}

static void affiche_table(Couple *table, int nCouples)
{
    int i;

    printf("---- Table des presents ----\n");
    for (i = 0; i < nCouples; i++) {
        printf("%2d : %s - %s\n", i + 1, addrip(table[i].ip), table[i].pseudo);
    }
    printf("----------------------------\n");
}

/*
   Envoi d'une commande locale au serveur.
*/
static int envoie_commande_locale(const char *buf, int taille)
{
    int sid;
    struct sockaddr_in serv;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return -1;
    }

    bzero(&serv, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PORT);
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (sendto(sid, buf, taille, 0, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
        perror("sendto");
        close(sid);
        return -1;
    }

    close(sid);
    return 0;
}

/*
   Fonction executee dans le processus fils.
   Elle reprend le coeur du serveur BEUIP.
*/
static void serveur_beuip_loop(const char *pseudo)
{
    int sid, n;
    int opt = 1;
    socklen_t ls;
    char buf[LBUF + 1];
    char msg[LBUF + 1];
    char ar[LBUF + 1];
    struct sockaddr_in sockConf;
    struct sockaddr_in sock;
    struct sockaddr_in sockBroad;
    Couple table[MAXCOUPLES];
    int nCouples = 0;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_BROADCAST");
        exit(EXIT_FAILURE);
    }

    bzero(&sockConf, sizeof(sockConf));
    sockConf.sin_family = AF_INET;
    sockConf.sin_port = htons(PORT);
    sockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&sockConf, sizeof(sockConf)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Serveur BEUIP actif sur le port %d avec le pseudo %s\n", PORT, pseudo);

    bzero(&sockBroad, sizeof(sockBroad));
    sockBroad.sin_family = AF_INET;
    sockBroad.sin_port = htons(PORT);
    sockBroad.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    construit_message_simple(msg, '1', pseudo);

    if (sendto(sid, msg, strlen(msg + 6) + 6, MSG_CONFIRM,
               (struct sockaddr *)&sockBroad, sizeof(sockBroad)) == -1) {
        perror("sendto broadcast");
    } else {
        printf("Broadcast d'identification envoye\n");
    }

    for (;;) {
        ls = sizeof(sock);

        n = recvfrom(sid, (void *)buf, LBUF, 0, (struct sockaddr *)&sock, &ls);

        if (n == -1) {
            perror("recvfrom");
            continue;
        }

        buf[n] = '\0';

#ifdef TRACE
        printf("[TRACE] Datagramme recu depuis %s\n",
               addrip(ntohl(sock.sin_addr.s_addr)));
#endif

        if (!message_valide(buf, n)) {
#ifdef TRACE
            printf("[TRACE] Message ignore : entete invalide\n");
#endif
            continue;
        }

        if ((buf[0] == '3' || buf[0] == '4' || buf[0] == '5') &&
            ntohl(sock.sin_addr.s_addr) != 0x7F000001) {
            printf("Commande refusee : origine non locale\n");
            continue;
        }

        if (buf[0] == '1' || buf[0] == '2' || buf[0] == '0') {
            char *pseudoRecu = buf + 6;

            if (buf[0] == '0') {
                supprime_couple_par_ip(table, &nCouples, ntohl(sock.sin_addr.s_addr));
#ifdef TRACE
                affiche_table(table, nCouples);
#endif
                continue;
            }

            ajoute_couple(table, &nCouples, ntohl(sock.sin_addr.s_addr), pseudoRecu);

#ifdef TRACE
            printf("[TRACE] Code recu : %c\n", buf[0]);
            printf("[TRACE] Pseudo recu : %s\n", pseudoRecu);
            affiche_table(table, nCouples);
#endif

            if (buf[0] == '1') {
                construit_message_simple(ar, '2', pseudo);

                if (sendto(sid, ar, strlen(ar + 6) + 6, MSG_CONFIRM,
                           (struct sockaddr *)&sock, ls) == -1) {
                    perror("sendto AR");
                }
            }

            continue;
        }

        if (buf[0] == '3') {
            affiche_table(table, nCouples);
            continue;
        }

        if (buf[0] == '4') {
            char *destPseudo = buf + 6;
            char *message = destPseudo + strlen(destPseudo) + 1;
            unsigned long ipDest;
            struct sockaddr_in dest;
            char msg9[LBUF + 1];

            ipDest = cherche_ip_par_pseudo(table, nCouples, destPseudo);

            if (ipDest == 0) {
                printf("Pseudo inconnu : %s\n", destPseudo);
                continue;
            }

            bzero(&dest, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_port = htons(PORT);
            dest.sin_addr.s_addr = htonl(ipDest);

            construit_message_simple(msg9, '9', message);

            if (sendto(sid, msg9, strlen(msg9 + 6) + 6, 0,
                       (struct sockaddr *)&dest, sizeof(dest)) == -1) {
                perror("sendto msg prive");
            }

            continue;
        }

        if (buf[0] == '9') {
            char *message = buf + 6;
            char *pseudoExp = cherche_pseudo_par_ip(table, nCouples, ntohl(sock.sin_addr.s_addr));

            if (pseudoExp != NULL) {
                printf("Message de %s : %s\n", pseudoExp, message);
            } else {
                printf("Message recu d'une IP inconnue : %s\n", message);
            }

            continue;
        }

        if (buf[0] == '5') {
            char *message = buf + 6;
            int i;
            char msg9[LBUF + 1];

            construit_message_simple(msg9, '9', message);

            for (i = 0; i < nCouples; i++) {
                struct sockaddr_in dest;

                if (strcmp(table[i].pseudo, pseudo) == 0) {
                    continue;
                }

                bzero(&dest, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_port = htons(PORT);
                dest.sin_addr.s_addr = htonl(table[i].ip);

                if (sendto(sid, msg9, strlen(msg9 + 6) + 6, 0,
                           (struct sockaddr *)&dest, sizeof(dest)) == -1) {
                    perror("sendto msg global");
                }
            }

            continue;
        }
    }
}

/*
   Fonctions exportees
*/

pid_t creme_get_server_pid(void)
{
    return serveur_pid;
}

int creme_beuip_start(const char *pseudo)
{
    pid_t pid;

    if (pseudo == NULL || *pseudo == '\0') {
        fprintf(stderr, "Pseudo manquant\n");
        return -1;
    }

    if (serveur_pid > 0) {
        fprintf(stderr, "Le serveur BEUIP est deja lance\n");
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        serveur_beuip_loop(pseudo);
        exit(EXIT_SUCCESS);
    }

    serveur_pid = pid;
    printf("Serveur BEUIP lance en fils pid=%d\n", (int)serveur_pid);
    return 0;
}

int creme_beuip_stop(void)
{
    if (serveur_pid <= 0) {
        fprintf(stderr, "Aucun serveur BEUIP actif\n");
        return -1;
    }

    if (kill(serveur_pid, SIGINT) == -1) {
        perror("kill");
        return -1;
    }

    waitpid(serveur_pid, NULL, 0);
    printf("Serveur BEUIP arrete\n");
    serveur_pid = -1;
    return 0;
}

int creme_mess_liste(void)
{
    char buf[LBUF + 1];

    construit_message_simple(buf, '3', NULL);
    return envoie_commande_locale(buf, 6);
}

int creme_mess_msg(const char *pseudo, const char *message)
{
    char buf[LBUF + 1];
    int taille;

    if (pseudo == NULL || message == NULL) {
        fprintf(stderr, "Parametres manquants pour mess msg\n");
        return -1;
    }

    taille = construit_message_prive(buf, pseudo, message);
    return envoie_commande_locale(buf, taille);
}

int creme_mess_all(const char *message)
{
    char buf[LBUF + 1];

    if (message == NULL) {
        fprintf(stderr, "Message manquant pour mess all\n");
        return -1;
    }

    construit_message_simple(buf, '5', message);
    return envoie_commande_locale(buf, 6 + strlen(message));
}
