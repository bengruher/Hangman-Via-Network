/**
 * @file client.cpp
 * @author Ben Gruher
 * @see "Seattle University, CPSC 3500, Spring 2019"
 * 
 * Client program of hangman game, communicates with server program server.cpp
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cerrno>
#include <cstdlib> // for atoi()
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
using namespace std;

const int BUFFER_SIZE = 1024;
const int IPV4_SIZE = INET_ADDRSTRLEN;
const char DELIMITER = '*';
const int NUM_DECIMAL_PLACES = 2;

void errorAndExit(const char* msg) {
    cout << msg << endl;
    exit(1);
}

int main(int argc, char *argv[])
{
    //error condition: too few arguments
    if (argc < 3) 
       errorAndExit("ERROR, missing port number or IP Address as argument\nUsage: ./game_client [IP Address] [PortNo]\n");

    struct sockaddr_in serv_info;

    serv_info.sin_family = AF_INET;
    serv_info.sin_port = htons(atoi(argv[2]));

    int error = inet_pton(AF_INET, argv[1], &serv_info.sin_addr);

    // error condition: call to inet_pton fails 
    if(error == 0) 
        errorAndExit("Invalid IP Address");
    if(error == -1) 
        errorAndExit("Invalid IP Family");

    //create client socket
    int fileDescript = socket(serv_info.sin_family, SOCK_STREAM, 0);

    //error condition: could not create socket
    if(fileDescript == -1) 
        errorAndExit("ERROR, could not create socket");
    
    error = connect(fileDescript, (struct sockaddr*)&serv_info, sizeof(serv_info));

    // error condition: could not connect socket
    if(error == -1) 
        errorAndExit("ERROR, could not connect socket");

    cout << "Enter name: ";
    string name;
    cin >> name;
    name += DELIMITER;
    error = send(fileDescript, name.c_str(), strlen(name.c_str()), 0);

    // error condition: send failure
    if(error <= 0) 
        errorAndExit("ERROR, could not send message");

    // receive word
    string word;
    int wordLength;
    int msgLength;
    char buffer[BUFFER_SIZE];
    bool stillReceiving = true;
    int totalData = 0; // keeps track of where end of last recv left off in buffer and equals the total number of bytes received in loop
    while(stillReceiving) {
        msgLength = recv(fileDescript, buffer + totalData, BUFFER_SIZE - totalData, 0);
        
        //error condition: recv failure
        if(msgLength <= 0) 
            errorAndExit("ERROR, could not receive message");

        for(int i = totalData; i < msgLength + totalData; i++) {
            if(buffer[i] == DELIMITER) {
                stillReceiving = false; // if the delimiter is in buffer, exit loop
                totalData--;
                buffer[i] = 0x00;
            }
        }
        totalData += msgLength;
    }
    
    word = (string)buffer;
    wordLength = word.length();

    int turnNum = 1;
    bool guessWord[wordLength];
    for(int i = 0; i < wordLength; i++)
        guessWord[i] = false;

    bool gameWon = false;
    while(!gameWon) { 
        cout << "Turn " << turnNum << endl;
        for(int i = 0; i < wordLength; i++) {
            if(guessWord[i])
                cout << word[i];
            else
                cout << "-";
        }
        cout << "\nEnter guess: ";
        string guess;
        cin >> guess;
        for(int i = 0; i < guess.length(); i++)
            guess[i] = toupper(guess[i]);
        error = send(fileDescript, guess.c_str(), strlen(guess.c_str()), 0);

        //error condition: send failure
        if(error <= 0) 
            errorAndExit("ERROR, could not send message");

        turnNum++;
        
        // receive result of guess
        // result will arrive in following format:
        // [1B - game won/not] [1B - guess correct/incorrect] [XB - positions of found letters]
        stillReceiving = true;
        totalData = 0;
        while(stillReceiving) {
            msgLength = recv(fileDescript, buffer + totalData, BUFFER_SIZE - totalData, 0);

            //error condition: recv failure
            if(msgLength <= 0) 
                errorAndExit("ERROR, could not receive message");

            for(int i = totalData; i < msgLength + totalData; i++) {
                if(buffer[i] == DELIMITER) {
                    stillReceiving = false; // if the delimiter is in buffer, exit loop
                    totalData = i + 1;
                    buffer[i] = 0x00;
                }
            }
        }
        gameWon = (buffer[0] == (char)1);
        bool successfulGuess = (buffer[1] == (char)1);
        if(successfulGuess)
            cout << "Correct!" << endl;
        else
            cout << "Incorrect Guess!" << endl;

        char* letterPositions = buffer;
        for(int i = 2; i < totalData; i++) {
            if(word[(int)letterPositions[i]] == guess[0]) {
                guessWord[(int)letterPositions[i]] = true;
            }
        }
    }
    cout << "CONGRATULATIONS - YOU WON!" << endl;
    cout << "The word was: " << word << endl;
    int guesses = turnNum - 1;
    cout << "Guesses: " << guesses << endl;
    double score = (double)guesses/(double)word.length();
    cout << "Score: " << score << endl;

    // receive leaderboard
    totalData = 0;
    stillReceiving = true;
    string leaderboardString = "";
    while(stillReceiving) {
        // if the delimiter is in buffer, exit loop
        msgLength = recv(fileDescript, buffer, BUFFER_SIZE, 0);

        //error condition: recv failure
        if(msgLength <= 0) 
            errorAndExit("ERROR, could not receive message");

        totalData += msgLength;

        for(int i = 0; i < msgLength; i++) {
            if(buffer[i] == DELIMITER)
                stillReceiving = false;
            else
                leaderboardString += buffer[i];
        }
    }

    cout << endl;
    for(int i = 0; i < totalData; i++) {
        cout << leaderboardString[i];
    }

    //close()
    close(fileDescript);

    return 0;
}