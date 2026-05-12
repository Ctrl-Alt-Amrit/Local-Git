/*
 * Project: Git-like File Versioning System (Persistent Version)
 * Course:  Data Structures and Algorithms Lab (CSCS3202)
 *
 * This version is an upgrade that includes:
 * - Persistence: All commits are saved to a log file ("git_history.log").
 * - History Loading: The log file is read on startup to rebuild the
 * in-memory stacks.
 * - Timestamps: Commits are now timestamped using <time.h>.
 *
 * This uses all the same core DSA concepts, but makes them persistent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // NEW: For getting date and time

// --- Data Structure Definitions ---

#define HISTORY_LOG_FILE "git_history.log"
#define MAX_LINE_LENGTH 1024

/*
 * Struct 1: CommitNode (The Stack Node)
 * MODIFIED: Added a timestamp.
 */
struct CommitNode {
    char message[256];
    char* codeContent;      // Stores a snapshot of the code
    int versionID;          // A unique ID for this commit
    char timestamp[64];     // NEW: To store the date/time string
    struct CommitNode* previous; // Pointer to the *previous* commit
};

/*
 * Struct 2: FileHistory (The Stack Manager)
 * Unchanged.
 */
struct FileHistory {
    char filename[256];
    struct CommitNode* top; // The stack for *this file*
    int nextVersionID;    // The *next* ID to assign on commit
};

// --- Global Variables ---

#define MAX_TRACKED_FILES 10
struct FileHistory trackedFiles[MAX_TRACKED_FILES];
int numTrackedFiles = 0;

// --- File I/O Helper Functions (Unchanged) ---

/*
 * FUNCTION: readFileContent
 * Reads the entire content of a file into a new string.
 */
char* readFileContent(char* filename) {
    FILE* file = fopen(filename, "r"); // "r" is fine for text files
    if (file == NULL) {
        // This is not an error, it just means the file doesn't exist yet
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length == 0) {
        fclose(file);
        char* emptyContent = (char*)malloc(1);
        if (emptyContent) emptyContent[0] = '\0';
        return emptyContent;
    }

    char* buffer = (char*)malloc(length + 1);
    if (buffer == NULL) {
        printf("Error: Not enough memory to read file.\n");
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    return buffer;
}

/*
 * FUNCTION: writeFileContent
 * Replaces the content of a file with the provided string.
 */
void writeFileContent(char* filename, char* content) {
    // "w" mode creates the file if it doesn't exist, or truncates it if it does
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        printf("Error: Could not open file %s for writing.\n", filename);
        return;
    }

    if (content != NULL) {
        fputs(content, file);
    }
    
    fclose(file);
}

// --- History Management Function (Unchanged) ---

/*
 * FUNCTION: getFileHistory
 * Finds or creates a history manager for a file.
 */
struct FileHistory* getFileHistory(char* filename, int createIfNotFound) {
    // 1. Loop to find if we already track this file
    for (int i = 0; i < numTrackedFiles; i++) {
        if (strcmp(trackedFiles[i].filename, filename) == 0) {
            return &trackedFiles[i];
        }
    }

    if (createIfNotFound) {
        if (numTrackedFiles >= MAX_TRACKED_FILES) {
            printf("Error: Cannot track more than %d files.\n", MAX_TRACKED_FILES);
            return NULL;
        }

        int i = numTrackedFiles;
        strcpy(trackedFiles[i].filename, filename);
        trackedFiles[i].top = NULL;
        trackedFiles[i].nextVersionID = 1;
        numTrackedFiles++;
        
        if (filename != NULL) { // Avoid printing during initial load
            printf("Now tracking file: %s\n", filename);
        }
        return &trackedFiles[i];
    }

    return NULL;
}

// --- NEW: History Log Functions (Save & Load) ---

/*
 * FUNCTION: resaveHistoryLog
 * NEW: Rewrites the entire log file based on the current in-memory stacks.
 * This ensures the log file is always in sync with the program state.
 */
void resaveHistoryLog() {
    FILE* logFile = fopen(HISTORY_LOG_FILE, "w");
    if (logFile == NULL) {
        printf("CRITICAL ERROR: Could not write to log file %s.\n", HISTORY_LOG_FILE);
        return;
    }

    for (int i = 0; i < numTrackedFiles; i++) {
        struct FileHistory* history = &trackedFiles[i];
        if (history->top == NULL) continue; // Skip files with no history

        // Stacks are LIFO (New->Old). We must write to log FIFO (Old->New).
        // 1. Traverse stack to get count
        int count = 0;
        struct CommitNode* current = history->top;
        while (current) {
            count++;
            current = current->previous;
        }

        // 2. Create a temporary array of pointers
        struct CommitNode** tempArray = (struct CommitNode**)malloc(count * sizeof(struct CommitNode*));
        if (tempArray == NULL) {
             printf("Error: Malloc failed during log save.\n");
             continue; // Skip this file
        }

        // 3. Traverse stack again, filling array (Newest is at index 0)
        current = history->top;
        for(int j = 0; j < count; j++) {
            tempArray[j] = current;
            current = current->previous;
        }

        // 4. Write to log file by iterating *backwards* through the array
        for(int j = count - 1; j >= 0; j--) {
            current = tempArray[j];
            fprintf(logFile, "--- BEGIN COMMIT ---\n");
            fprintf(logFile, "FILE: %s\n", history->filename);
            fprintf(logFile, "VERSION: %d\n", current->versionID);
            fprintf(logFile, "TIME: %s", current->timestamp); // Timestamp has newline
            fprintf(logFile, "MESSAGE: %s\n", current->message);
            fprintf(logFile, "--- BEGIN CONTENT ---\n");
            if (current->codeContent != NULL) {
                fprintf(logFile, "%s", current->codeContent);
            }
            fprintf(logFile, "\n--- END CONTENT ---\n"); // Add newline for safety
            fprintf(logFile, "--- END COMMIT ---\n");
        }
        
        free(tempArray);
    }

    fclose(logFile);
}

/*
 * FUNCTION: loadHistoryLog
 * NEW: Reads the log file on startup to rebuild the in-memory stacks.
 */
void loadHistoryLog() {
    FILE* logFile = fopen(HISTORY_LOG_FILE, "r");
    if (logFile == NULL) {
        printf("No history log found. Starting fresh.\n");
        return;
    }

    char line[MAX_LINE_LENGTH];
    char filename[256] = {0};
    char message[256] = {0};
    char timestamp[64] = {0};
    int version = 0;

    char* contentBuffer = NULL;
    size_t contentSize = 0;
    int inContent = 0;

    while (fgets(line, sizeof(line), logFile)) {
        if (strstr(line, "--- BEGIN COMMIT ---")) {
            // Reset for new commit
            strcpy(filename, "");
            strcpy(message, "");
            strcpy(timestamp, "");
            version = 0;
            if (contentBuffer) free(contentBuffer);
            contentBuffer = NULL;
            contentSize = 0;
            inContent = 0;
        } else if (strstr(line, "FILE: ")) {
            sscanf(line, "FILE: %[^\n]", filename);
        } else if (strstr(line, "VERSION: ")) {
            sscanf(line, "VERSION: %d", &version);
        } else if (strstr(line, "TIME: ")) {
            strcpy(timestamp, line + 6); // Copy "TIME: " + 6 chars
        } else if (strstr(line, "MESSAGE: ")) {
            sscanf(line, "MESSAGE: %[^\n]", message);
        } else if (strstr(line, "--- BEGIN CONTENT ---")) {
            inContent = 1;
            contentBuffer = (char*)malloc(1);
            contentBuffer[0] = '\0';
            contentSize = 1;
        } else if (strstr(line, "--- END CONTENT ---")) {
            inContent = 0;
        } else if (inContent) {
            size_t lineLen = strlen(line);
            char* newBuffer = (char*)realloc(contentBuffer, contentSize + lineLen);
            if (newBuffer == NULL) {
                printf("Error: realloc failed during history load.\n");
                if (contentBuffer) free(contentBuffer);
                contentBuffer = NULL;
                continue;
            }
            contentBuffer = newBuffer;
            strcpy(contentBuffer + contentSize - 1, line);
            contentSize += lineLen;
        } else if (strstr(line, "--- END COMMIT ---")) {
            // End of commit, build the node and push to stack
            if (strlen(filename) > 0) {
                struct FileHistory* history = getFileHistory(filename, 1);
                struct CommitNode* newNode = (struct CommitNode*)malloc(sizeof(struct CommitNode));
                
                if (newNode == NULL) {
                    printf("Error: Malloc failed during history load.\n");
                    if (contentBuffer) free(contentBuffer);
                    continue;
                }
                
                strcpy(newNode->message, message);
                strcpy(newNode->timestamp, timestamp);
                newNode->versionID = version;
                newNode->codeContent = contentBuffer; // Transfer ownership
                
                // Push onto stack
                newNode->previous = history->top;
                history->top = newNode;

                // Update next version ID
                if (version >= history->nextVersionID) {
                    history->nextVersionID = version + 1;
                }
                
                contentBuffer = NULL; // Avoid double free
            }
        }
    }
    if (contentBuffer) free(contentBuffer);
    fclose(logFile);
    printf("Successfully loaded commit history from %s.\n", HISTORY_LOG_FILE);
}

// --- Core Stack/Project Functions ---

/*
 * FUNCTION: commit (Stack PUSH)
 * MODIFIED: Gets timestamp and calls resaveHistoryLog()
 */
void commit(struct FileHistory* history, char* msg) {
    char* currentCode = readFileContent(history->filename);
    if (currentCode == NULL) {
        currentCode = (char*)malloc(1);
        if (currentCode) currentCode[0] = '\0';
        else {
            printf("Error: Memory allocation failed.\n");
            return;
        }
    }
    
    struct CommitNode* newNode = (struct CommitNode*)malloc(sizeof(struct CommitNode));
    if (newNode == NULL) {
        printf("Error: Not enough memory to create commit node.\n");
        free(currentCode);
        return;
    }

    // NEW: Get current time
    time_t now = time(NULL);
    char* timeStr = ctime(&now);

    // Set data
    strcpy(newNode->message, msg);
    strcpy(newNode->timestamp, timeStr); // Store timestamp
    newNode->codeContent = currentCode;
    newNode->versionID = history->nextVersionID++;

    // Push to in-memory stack
    newNode->previous = history->top;
    history->top = newNode;

    printf("\nSuccessfully committed version %d for %s: \"%s\"\n", 
           newNode->versionID, history->filename, newNode->message);

    // NEW: Save the entire history log
    resaveHistoryLog();
}

/*
 * FUNCTION: revert (Stack POP)
 * MODIFIED: Calls resaveHistoryLog() after reverting.
 */
void revert(struct FileHistory* history) {
    if (history->top == NULL) {
        printf("\nError: No commits to revert for %s.\n", history->filename);
        return;
    }

    struct CommitNode* temp = history->top;
    history->top = history->top->previous;

    char* codeToRestore = (history->top == NULL) ? "" : history->top->codeContent;
    
    // Write restored code back to the file
    writeFileContent(history->filename, codeToRestore);

    printf("\nReverted %s to version %d.\n", 
           history->filename, (history->top == NULL) ? 0 : history->top->versionID);
    
    // Free the popped commit
    free(temp->codeContent);
    free(temp);

    // NEW: Resave the log file with this commit removed
    resaveHistoryLog();
}

/*
 * FUNCTION: showHistory (Stack DISPLAY)
 * MODIFIED: Now shows timestamp.
 */
void showHistory(struct FileHistory* history) {
    if (history == NULL || history->top == NULL) {
        printf("\n--- No commit history for %s. ---\n", 
               (history == NULL) ? "file" : history->filename);
        return;
    }

    printf("\n--- Commit History for %s (Newest to Oldest) ---\n", history->filename);
    struct CommitNode* current = history->top;
    while (current != NULL) {
        // Remove newline from timestamp for cleaner printing
        current->timestamp[strcspn(current->timestamp, "\n")] = 0;
        printf("Version %d [%s] - \"%s\"\n", 
               current->versionID, current->timestamp, current->message);
        current = current->previous;
    }
    printf("---------------------------------------------------\n");
}

/*
 * FUNCTION: freeHistory (Utility for one stack)
 */
void freeHistory(struct FileHistory* history) {
    struct CommitNode* temp;
    while (history->top != NULL) {
        temp = history->top;
        history->top = history->top->previous;
        free(temp->codeContent); 
        free(temp);
    }
}

/*
 * FUNCTION: freeAllHistories (Utility for clean exit)
 */
void freeAllHistories() {
    printf("Cleaning up memory for all tracked files...\n");
    for (int i = 0; i < numTrackedFiles; i++) {
        freeHistory(&trackedFiles[i]);
    }
}


// --- Main Program Loop (Helpers Unchanged) ---

/*
 * FUNCTION: simulateFileEdit
 */
void simulateFileEdit(char* filename) {
    char lineBuffer[256];
    FILE* file = fopen(filename, "a");
    if (file == NULL) {
        printf("Error: Could not open %s to edit.\n", filename);
        return;
    }
    printf("Enter new line of code (adds to end of file): ");
    if (fgets(lineBuffer, sizeof(lineBuffer), stdin) != NULL) {
        fputs(lineBuffer, file);
        printf("Line added to %s.\n", filename);
    }
    fclose(file);
}

/*
 * FUNCTION: showCurrentFile
 */
void showCurrentFile(char* filename) {
    char* content = readFileContent(filename);
    printf("\n--- Current Content of %s ---\n", filename);
    if (content == NULL || strlen(content) == 0) {
        printf("(File is empty or does not exist)\n");
    } else {
        printf("%s", content);
    }
    printf("--------------------------------------\n");
    if (content) {
        free(content);
    }
}


int main() {
    // NEW: Load history from log file at the start
    loadHistoryLog();

    int mainChoice = 0;
    char filenameBuffer[256];
    struct FileHistory* currentHistory = NULL;

    while (mainChoice != 7) {
        printf("\n==================================\n");
        printf("  Git-like File Versioning System\n");
        printf("==================================\n");
        
        if (currentHistory == NULL) {
            // ... (menu logic unchanged) ...
            printf("No file selected.\n");
            printf("1. Select/Track File\n");
            printf("7. Exit\n");
            printf("Enter your choice: ");
        } else {
            // ... (menu logic unchanged) ...
            printf("Current File: %s (Ver: %d)\n", 
                   currentHistory->filename, 
                   (currentHistory->top == NULL) ? 0 : currentHistory->top->versionID);
            printf("----------------------------------\n");
            printf("1. Show Current Code\n");
            printf("2. Edit Code (Append a line)\n");
            printf("3. Commit Current Code\n");
            printf("4. Revert to Previous Commit\n");
            printf("5. Show Commit History\n");
            printf("6. Change File\n");
            printf("7. Exit\n");
            printf("Enter your choice: ");
        }

        if (scanf("%d", &mainChoice) != 1) {
            while (getchar() != '\n');
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        while (getchar() != '\n'); // Consume newline

        if (currentHistory == NULL) {
            // ... (main logic unchanged) ...
            if (mainChoice == 1) {
                printf("Enter filename to track (e.g., my_code.c): ");
                if (fgets(filenameBuffer, sizeof(filenameBuffer), stdin) != NULL) {
                    filenameBuffer[strcspn(filenameBuffer, "\n")] = 0;
                    currentHistory = getFileHistory(filenameBuffer, 1);
                    if (currentHistory == NULL) {
                        printf("Failed to track file. Limit may be reached.\n");
                    }
                }
            } else if (mainChoice == 7) {
                break;
            } else {
                printf("Please select a file first.\n");
            }
        } else {
            // ... (main logic unchanged) ...
            char messageBuffer[256];
            switch (mainChoice) {
                case 1:
                    showCurrentFile(currentHistory->filename);
                    break;
                case 2:
                    simulateFileEdit(currentHistory->filename);
                    break;
                case 3:
                    printf("Enter commit message: ");
                    if (fgets(messageBuffer, sizeof(messageBuffer), stdin) != NULL) {
                        messageBuffer[strcspn(messageBuffer, "\n")] = 0;
                        commit(currentHistory, messageBuffer);
                    }
                    break;
                case 4:
                    revert(currentHistory);
                    break;
                case 5:
                    showHistory(currentHistory);
                    break;
                case 6:
                    currentHistory = NULL;
                    break;
                case 7:
                    break;
                default:
                    printf("\nInvalid choice. Please try again.\n");
            }
        }
    }

    freeAllHistories(); // Clean up all memory
    printf("Exiting.\n");
    return 0;
}

