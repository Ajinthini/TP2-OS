/*****
* Serveur BEUIP
* version etendue avec :
* - identification broadcast
* - liste locale
* - message a un pseudo
* - message a tout le monde
* - notification de depart
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT 9998
#define LBUF 512
#define MAXCOUPLES 255
#define BROADCAST_IP "192.168.88.255"

typedef struct {
    unsigned long ip;
    char pseudo[256];
} Couple;

char buf[LBUF + 1];
struct sockaddr_in SockConf;
static Couple Table[MAXCOUPLES];
static int NCouples = 0;
static int sidGlobal = -1;
static char MonPseudo[256];

/*
   Cette fonction transforme une adresse IP en chaine lisible.
*/
char *addrip(unsigned long A)
{
    static char b[16];

    sprintf(b, "%u.%u.%u.%u",
            (unsigned int)(A >> 24 & 0xFF),
            (unsigned int)(A >> 16 & 0xFF),
            (unsigned int)(A >> 8 & 0xFF),
            (unsigned int)(A & 0xFF));

    return b;
}

/*
   Construit un message BEUIP simple :
   octet 0 : code
   octets 1-5 : "BEUIP"
   octets 6-fin : donnees
*/
void construitMessage(char *dest, char code, char *donnees)
{
    dest[0] = code;
    strcpy(dest + 1, "BEUIP");

    if (donnees != NULL) {
        strcpy(dest + 6, donnees);
    } else {
        dest[6] = '\0';
    }
}

/*
   Verifie l'entete du message.
*/
int messageValide(char *msg, int n)
{
    if (n < 6) {
        return 0;
    }

    if (strncmp(msg + 1, "BEUIP", 5) != 0) {
        return 0;
    }

    return 1;
}

/*
   Verifie si un couple est deja present.
*/
int dejaPresent(unsigned long ip, char *pseudo)
{
    int i;

    for (i = 0; i < NCouples; i++) {
        if (Table[i].ip == ip && strcmp(Table[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }

    return 0;
}

/*
   Ajoute un couple dans la table si absent.
*/
void ajouteCouple(unsigned long ip, char *pseudo)
{
    if (pseudo == NULL || *pseudo == '\0') {
        return;
    }

    if (dejaPresent(ip, pseudo)) {
#ifdef TRACE
        printf("[TRACE] Couple deja connu : %s - %s\n", addrip(ip), pseudo);
#endif
        return;
    }

    if (NCouples >= MAXCOUPLES) {
        fprintf(stderr, "Table des couples pleine\n");
        return;
    }

    Table[NCouples].ip = ip;
    strncpy(Table[NCouples].pseudo, pseudo, sizeof(Table[NCouples].pseudo) - 1);
    Table[NCouples].pseudo[sizeof(Table[NCouples].pseudo) - 1] = '\0';
    NCouples++;

#ifdef TRACE
    printf("[TRACE] Ajout couple : %s - %s\n", addrip(ip), pseudo);
#endif
}

/*
   Supprime un couple a partir de l'IP.
*/
void supprimeCoupleParIP(unsigned long ip)
{
    int i, j;

    for (i = 0; i < NCouples; i++) {
        if (Table[i].ip == ip) {
#ifdef TRACE
            printf("[TRACE] Suppression couple : %s - %s\n",
                   addrip(Table[i].ip), Table[i].pseudo);
#endif
            for (j = i; j < NCouples - 1; j++) {
                Table[j] = Table[j + 1];
            }
            NCouples--;
            return;
        }
    }
}

/*
   Cherche le pseudo correspondant a une IP.
*/
char *cherchePseudoParIP(unsigned long ip)
{
    int i;

    for (i = 0; i < NCouples; i++) {
        if (Table[i].ip == ip) {
            return Table[i].pseudo;
        }
    }

    return NULL;
}

/*
   Cherche l'IP correspondant a un pseudo.
*/
unsigned long chercheIPParPseudo(char *pseudo)
{
    int i;

    for (i = 0; i < NCouples; i++) {
        if (strcmp(Table[i].pseudo, pseudo) == 0) {
            return Table[i].ip;
        }
    }

    return 0;
}

/*
   Affiche la table des presents.
*/
void afficheTable(void)
{
    int i;

    printf("---- Table des presents ----\n");
    for (i = 0; i < NCouples; i++) {
        printf("%2d : %s - %s\n", i + 1, addrip(Table[i].ip), Table[i].pseudo);
    }
    printf("----------------------------\n");
}

/*
   Envoie un message broadcast de depart (code '0')
   avant de quitter.
*/
void envoieDepartEtQuitte(int sig)
{
    struct sockaddr_in SockBroad;
    char msg[LBUF + 1];

    (void)sig;

    if (sidGlobal >= 0) {
        bzero(&SockBroad, sizeof(SockBroad));
        SockBroad.sin_family = AF_INET;
        SockBroad.sin_port = htons(PORT);
        SockBroad.sin_addr.s_addr = inet_addr(BROADCAST_IP);

        construitMessage(msg, '0', MonPseudo);

        sendto(sidGlobal, msg, strlen(msg + 6) + 6, 0,
               (struct sockaddr *)&SockBroad, sizeof(SockBroad));
    }

    printf("\nArret du serveur BEUIP\n");
    exit(0);
}

int main(int N, char *P[])
{
    int sid, n;
    int opt = 1;
    socklen_t ls;
    char msg[LBUF + 1];
    char ar[LBUF + 1];
    char *pseudo;
    struct sockaddr_in Sock;
    struct sockaddr_in SockBroad;

    if (N != 2) {
        fprintf(stderr, "Utilisation : %s pseudo\n", P[0]);
        return 1;
    }

    pseudo = P[1];
    strncpy(MonPseudo, pseudo, sizeof(MonPseudo) - 1);
    MonPseudo[sizeof(MonPseudo) - 1] = '\0';

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return 2;
    }

    sidGlobal = sid;

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_BROADCAST");
        return 3;
    }

    signal(SIGINT, envoieDepartEtQuitte);

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        return 4;
    }

    printf("Serveur BEUIP actif sur le port %d avec le pseudo %s\n", PORT, pseudo);

    bzero(&SockBroad, sizeof(SockBroad));
    SockBroad.sin_family = AF_INET;
    SockBroad.sin_port = htons(PORT);
    SockBroad.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    /*
       Envoi initial du broadcast d'identification.
    */
    construitMessage(msg, '1', pseudo);

    if (sendto(sid, msg, strlen(msg + 6) + 6, MSG_CONFIRM,
               (struct sockaddr *)&SockBroad, sizeof(SockBroad)) == -1) {
        perror("sendto broadcast");
    } else {
        printf("Broadcast d'identification envoye\n");
    }

    for (;;) {
        ls = sizeof(Sock);

        n = recvfrom(sid, (void *)buf, LBUF, 0,
                     (struct sockaddr *)&Sock, &ls);

        if (n == -1) {
            perror("recvfrom");
            continue;
        }

        buf[n] = '\0';

#ifdef TRACE
        printf("[TRACE] Datagramme recu depuis %s\n",
               addrip(ntohl(Sock.sin_addr.s_addr)));
#endif

        if (!messageValide(buf, n)) {
#ifdef TRACE
            printf("[TRACE] Message ignore : entete invalide\n");
#endif
            continue;
        }

        /*
           Verification de securite :
           les commandes 3, 4 et 5 doivent venir de 127.0.0.1
        */
        if ((buf[0] == '3' || buf[0] == '4' || buf[0] == '5') &&
            ntohl(Sock.sin_addr.s_addr) != 0x7F000001) {
            printf("Commande refusee : origine non locale\n");
            continue;
        }

        /*
           Gestion des messages d'identification / AR / depart
        */
        if (buf[0] == '1' || buf[0] == '2' || buf[0] == '0') {
            char *pseudoRecu = buf + 6;

            if (buf[0] == '0') {
                supprimeCoupleParIP(ntohl(Sock.sin_addr.s_addr));
#ifdef TRACE
                afficheTable();
#endif
                continue;
            }

            ajouteCouple(ntohl(Sock.sin_addr.s_addr), pseudoRecu);

#ifdef TRACE
            printf("[TRACE] Code recu : %c\n", buf[0]);
            printf("[TRACE] Pseudo recu : %s\n", pseudoRecu);
            afficheTable();
#endif

            if (buf[0] == '1') {
                construitMessage(ar, '2', pseudo);

                if (sendto(sid, ar, strlen(ar + 6) + 6, MSG_CONFIRM,
                           (struct sockaddr *)&Sock, ls) == -1) {
                    perror("sendto AR");
                } else {
#ifdef TRACE
                    printf("[TRACE] AR envoye a %s\n",
                           addrip(ntohl(Sock.sin_addr.s_addr)));
#endif
                }
            }

            continue;
        }

        /*
           Code '3' : liste
        */
        if (buf[0] == '3') {
            afficheTable();
            continue;
        }

        /*
           Code '4' : message a un pseudo
           Le contenu apres BEUIP est :
           pseudo\0message
        */
        if (buf[0] == '4') {
            char *destPseudo = buf + 6;
            char *message = destPseudo + strlen(destPseudo) + 1;
            unsigned long ipDest;
            struct sockaddr_in Dest;
            char msg9[LBUF + 1];

            ipDest = chercheIPParPseudo(destPseudo);

            if (ipDest == 0) {
                printf("Pseudo inconnu : %s\n", destPseudo);
                continue;
            }

            bzero(&Dest, sizeof(Dest));
            Dest.sin_family = AF_INET;
            Dest.sin_port = htons(PORT);
            Dest.sin_addr.s_addr = htonl(ipDest);

            construitMessage(msg9, '9', message);

            if (sendto(sid, msg9, strlen(msg9 + 6) + 6, 0,
                       (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                perror("sendto msg prive");
            }

            continue;
        }

        /*
           Code '9' : reception d'un message prive ou global
        */
        if (buf[0] == '9') {
            char *message = buf + 6;
            char *pseudoExp = cherchePseudoParIP(ntohl(Sock.sin_addr.s_addr));

            if (pseudoExp != NULL) {
                printf("Message de %s : %s\n", pseudoExp, message);
            } else {
                printf("Message recu d'une IP inconnue : %s\n", message);
            }

            continue;
        }

        /*
           Code '5' : message a tout le monde
           On le re-expedie a tous les couples sauf soi-meme.
        */
        if (buf[0] == '5') {
            char *message = buf + 6;
            int i;
            char msg9[LBUF + 1];

            construitMessage(msg9, '9', message);

            for (i = 0; i < NCouples; i++) {
                struct sockaddr_in Dest;

                if (strcmp(Table[i].pseudo, MonPseudo) == 0) {
                    continue;
                }

                bzero(&Dest, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr.s_addr = htonl(Table[i].ip);

                if (sendto(sid, msg9, strlen(msg9 + 6) + 6, 0,
                           (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                    perror("sendto msg global");
                }
            }

            continue;
        }
    }

    return 0;
}
