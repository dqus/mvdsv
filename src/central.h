
#ifndef CENTRAL_H
#define CENTRAL_H

void Central_Init(void);
void Central_Shutdown(void);
void Central_ProcessResponses(void);
// void Central_SubmitGame(const char* path);

// Creates a challenge/response on the web server after user claims to be 'username'
void Central_GenerateChallenge(client_t* client, const char* username, qbool during_login);

// Checks with the server if a client's response to a challenge is correct
void Central_VerifyChallengeResponse(client_t* client, const char* challenge, const char* response);

// Preserves an in-flight web-login request when QCX moves a live connection
// between physical client slots.
void Central_SwapClientPointers(client_t* left, client_t* right);

#endif // !CENTRAL_H
