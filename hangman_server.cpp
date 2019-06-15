/**
 * @file server.cpp
 * @author Ben Gruher
 * @see "Seattle University, CPSC 3500, Spring 2019"
 * 
 * Server side program of hangman game, communicates with client program client.cpp
 */

#include <sys/socket.h> //for socket(), connect() recv(), send(), 
#include <cstdlib>      // for atoi(), exit(), rand(), and srand()
#include <cstring>      // for memset() and getline(), and to_string()
#include <arpa/inet.h>  // for sockaddr_in and inet_ntoa()
#include <unistd.h>     // for close()
#include <pthread.h>    // for pthread_create() and pthread_join()
#include <ctime>        // for time(0)
#include <cmath>        // for pow()
#include <fstream>      // for ifstream
#include <iostream>     // for cout
#define MAXPENDING 5    // Maximum outstanding connection requests
using namespace std;

const int BUFFER_SIZE = 1024;
const int NUM_WORDS = 57127;
const string wordFile = "/home/fac/lillethd/cpsc3500/projects/p4/words.txt";
const int NUM_SCORES_DISPLAYED = 3;
const char DELIMITER = '*';
const int NUM_DECIMAL_PLACES = 2;

struct leaderboard_entry {
    double score;
    string name;
};

struct leaderboard {
    int numEntries;
    leaderboard_entry topScores[NUM_SCORES_DISPLAYED];
};

struct wordGame {
    wordGame(string guessWord) {
        word = guessWord;
        won = false;
        numGuesses = 0;
        numSucLetters = 0;
        wordLength = word.length();
        partialSolution = new bool[wordLength];
        for(int i = 0; i < wordLength; i++)
            partialSolution[i] = false;
    }
    ~wordGame() {
        delete[] partialSolution;
    }
    bool won;
    string word;
    int wordLength;
    int numGuesses;
    int numSucLetters;
    bool* partialSolution;
};

struct clientGame {
    string clientName;
    wordGame* currentWord;
};

leaderboard leaders;
pthread_mutex_t leaderboard_mutex;

void errorAndExit(const char* msg) {
    cout << msg << endl;
    exit(1);
}

int acceptConnection(int servSock) {
    struct sockaddr_in client_info;
    unsigned int client_addr_length = sizeof(client_info);

    int clientSock = accept(servSock, (struct sockaddr*)&client_info, &client_addr_length);
    
    // error condition: failed to accept
    if(clientSock == -1) 
        errorAndExit("ERROR, could not accept client");

    cout << "Handling client: " << inet_ntoa(client_info.sin_addr) << endl;

    return clientSock;
}

int createServerSock(int portNo) {
    int opt = 1;

    // set sockaddr_in structure for IPV4 Addresses 
    struct sockaddr_in serv_info;
    memset(&serv_info, 0, sizeof(serv_info));
    serv_info.sin_family = AF_INET;
    serv_info.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_info.sin_port = portNo;
    
    // create listening socket
    int serverFD = socket(AF_INET, SOCK_STREAM, 0);

    // error condition: could not create socket
    if(serverFD == -1) 
        errorAndExit("ERROR, could not create socket");

    int error = setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // error condition: setsockopt fails
    if(error == -1) 
        errorAndExit("ERROR, could not set sock options");

    error = bind(serverFD, (struct sockaddr*)&serv_info, sizeof(serv_info));

    // error condition: bind failure
    if(error == -1) 
        cout << strerror(errno) << endl;
        // errorAndExit("ERROR, could not bind socket");

    error = listen(serverFD, MAXPENDING);

    // error condition: listen failure
    if(error == -1) 
        errorAndExit("ERROR, could not listen on socket");

    return serverFD;
}

// inserts new high score and pushes back scores worse than score
void pushbackScores(int start, double score, string name) {
    for(int j = leaders.numEntries - 2; j >= start; j--) {
        leaders.topScores[j + 1] = leaders.topScores[j];
    }
    leaders.topScores[start].score = score;
    leaders.topScores[start].name = name;
}

void updateLeaderboard(double score, string name) {
    pthread_mutex_lock(&leaderboard_mutex);
    for(int i = 0; i < NUM_SCORES_DISPLAYED; i++) {
        if(i > (leaders.numEntries - 1)) {
            leaders.numEntries++;
            leaders.topScores[i].score = score;
            leaders.topScores[i].name = name;
            pthread_mutex_unlock(&leaderboard_mutex);
            return;
        }
        else if(score < leaders.topScores[i].score) {
            if(leaders.numEntries == NUM_SCORES_DISPLAYED) {
                pushbackScores(i, score, name);
                pthread_mutex_unlock(&leaderboard_mutex);
                return; 
            }
            else {
                leaders.numEntries++;
                pushbackScores(i, score, name);
                pthread_mutex_unlock(&leaderboard_mutex);
                return;
            }
        }
    }
}

void handleClient(int clientFD) {
    clientGame currGame;
    char buffer[BUFFER_SIZE];

    int msgLength = 0;
    currGame.clientName = "";

    // receive name
    bool stillReceiving = true;
    while(stillReceiving) {
        // if the delimiter is in buffer, exit loop
        msgLength = recv(clientFD, buffer, BUFFER_SIZE, 0);

        //error condition: recv failure
        if(msgLength <= 0) 
            errorAndExit("ERROR, could not receive message");

        for(int i = 0; i < msgLength; i++) {
            if(buffer[i] == DELIMITER)
                stillReceiving = false;
            else
                currGame.clientName += buffer[i];
        }
    }
    cout << "Client Name: " << currGame.clientName << endl;

    int rndLine = (rand() % NUM_WORDS) + 1;
    string guessWord;
    ifstream filestream;
    filestream.open(wordFile);
    for(int i = 0; i < rndLine; i++) {
        getline(filestream, guessWord);
    }
    wordGame newGame(guessWord);
    cout << "Current Word: " << guessWord << endl;
    currGame.currentWord = &newGame;

    // send word
    int error = send(clientFD, (guessWord + DELIMITER).c_str(), strlen((guessWord + '*').c_str()), 0);

    // error condition: send failure
    if(error <= 0)
        errorAndExit("ERROR, could not send message");

    while(!newGame.won) {
        // receive guess from client and store in buffer[0]
        msgLength = 0;
        while(msgLength == 0)
            msgLength = recv(clientFD, buffer, BUFFER_SIZE, 0);
        
        // error condition: receive failure
        if(msgLength <= 0) 
                errorAndExit("ERROR, could not receive message");

        bool successfulGuess = false;
        char guess = buffer[0];
        currGame.currentWord->numGuesses++;
        string guessReturn = "";
        for(int i = 0; i < currGame.currentWord->wordLength; i++) {
            if(currGame.currentWord->word[i] == guess) {
                currGame.currentWord->partialSolution[i] = true;
                successfulGuess = true;
                currGame.currentWord->numSucLetters++;
                guessReturn += (char)i;
            }
        }        

        // if won, indicate with ASCII(1) at beginning of msg
        if(currGame.currentWord->numSucLetters == guessWord.length()) {
            guessReturn.insert(guessReturn.begin(), (char)1);
            newGame.won = true;
        }

        // if not won, indicate with ASCII(0) at beginning of msg
        else
            guessReturn.insert(guessReturn.begin(), (char)0);

        // if correct guess, indicate with ASCCI(1) at second position
        if(successfulGuess)
            guessReturn.insert(guessReturn.begin() + 1, (char)1);

        // if not correct guess, indicate with ASCII(0) at second position
        else
            guessReturn.insert(guessReturn.begin() + 1, (char)0);

        // send result of guess
        guessReturn += DELIMITER;
        
        error = send(clientFD, guessReturn.c_str(), sizeof(guessReturn.c_str()), 0);

        // error condition: send failure
        if(error <= 0)
            errorAndExit("ERROR, could not send message");
    }

    double score = (double)newGame.numGuesses / (double)currGame.currentWord->wordLength;
    updateLeaderboard(score, currGame.clientName);
    cout << "Score = " << score << endl;

    string leaderboardString = "\nLEADERBOARD:\n-------------------\n";
    for(int i = 0; i < leaders.numEntries; i++) {
        leaderboardString += to_string(i + 1);
        leaderboardString += ":\n";
        leaderboardString += leaders.topScores[i].name;
        leaderboardString += "\n";
        leaderboardString += to_string(leaders.topScores[i].score);
        leaderboardString += "\n\n";
    }
    leaderboardString += DELIMITER;
    error = send(clientFD, leaderboardString.c_str(), leaderboardString.length(), 0);
    
    // error condition: send failure
    if(error <= 0)
        errorAndExit("ERROR, could not send message");

    close(clientFD);
}

void* threadRoutine(void* threadArg) {
    int clientFD = *(long*)(&threadArg); 
    handleClient(clientFD);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) 
        errorAndExit("ERROR, no port provided\nUsage: ./game_server [PortNo]\n");

    leaders.numEntries = 0;
    srand(time(0));

    /* initialize mutex */
	int error = pthread_mutex_init(&leaderboard_mutex, NULL);
	if (error != 0) 
		errorAndExit("ERROR, could not initialize mutex");

    int serverFD = createServerSock(htons(atoi(argv[1])));
    
    // error condition: could not create server socket
    if(serverFD == -1) 
        errorAndExit("ERROR, could not create server socket");    
    
    pthread_t threadID;
    while(true) {
        int clientFD = acceptConnection(serverFD);
        pthread_create(&threadID, NULL, threadRoutine, (void*) (long)clientFD);
        pthread_detach(threadID);
    }

    return 0; 
}