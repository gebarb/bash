#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports five arguments

#define CLOSED 0	                //file is CLOSED

#define OPEN 1                  //file is open



struct __attribute__((packed)) DirectoryEntry {
  char DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t Unused1[8];
  uint16_t DIR_FirstClusterHigh;
  uint8_t Unused2[4];
  uint16_t DIR_FirstClusterLow;
  uint32_t DIR_FileSize;
};

struct fatSpec
{
    int16_t BPB_BytsPerSec;
    int8_t BPB_SecPerClus;
    int16_t BPB_RsvdSecCnt;
    int8_t BPB_NumFats;
    int32_t BPB_FATSz32;
    char VOL_Name[11];
};

struct DirectoryEntry dir[16];
FILE* writeFile;

void getInfo(FILE *fp, struct fatSpec* specs);
void printInfo(struct fatSpec* specs);
void stringToLower(char* str);
int fileNameCmp(char input[], char strp[]);
void popRootDir(FILE *fp, struct fatSpec* specs);
void stat(char input[]);

int LBAToOffset(int32_t sector, struct fatSpec* specs);
int16_t NextLB(uint32_t sector, struct fatSpec* specs, FILE* fp);
void get(char input[], struct fatSpec* specs, FILE* fp);
void fileRead(int startByte, int numBytes, char filename[], FILE* fp, struct fatSpec* specs);

int findFile(char *operand);


void ls();
void printVol(struct fatSpec* specs);

int main()
{
  //keeps track if the file is open or closed
  int filestat = CLOSED;
  FILE* fp;

  struct fatSpec* specs = (struct fatSpec*)malloc(sizeof(struct fatSpec));




  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

  while( 1 )
  {

    // Print out the msh prompt
    printf ("mfs> ");


    // the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    int   token_count = 0;

    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;

    char *working_str  = strdup( cmd_str );

    // we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

    // Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) &&
              (token_count<MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
        token_count++;
    }


    //The open command, takes the file name as the parameter, if file is already open
    //or file not found prints an error and gets ready for another command
    if(strcmp(token[0],"open")==0)
    {
        //file already open
        if(filestat == OPEN)
            printf("Error: File system image already open.\n");
        else
        {

            fp = fopen(token[1], "r");

            //file not found
            if(fp == NULL)
                printf("Error: File system image not found.\n");
            else
            {
                //set file to open
                filestat = OPEN;
                //read in the root directory

                getInfo(fp, specs);
                popRootDir(fp, specs);



            }

        }

    }

    //for all commands besides open, if the file is closed, print an error
    else if(filestat == CLOSED)
    {
        printf("Error: File system image not open.\n");
    }
    //closes the file
    else if(strcmp(token[0],"close")==0)
    {
        fclose(fp);
        filestat = CLOSED;
    }
    //calls the info command
    else if(strcmp(token[0],"info")==0)
    {
        printInfo(specs);

    }
    //prints out the attribute and file size of the given file if it exists
    else if(strcmp(token[0],"stat")==0)
    {

        stat(token[1]);
    }
    //adds the specified file to the working directory
    else if(strcmp(token[0],"get")==0)
    {
        if(token[1]==NULL)
            printf("Error: file not found\n");
        else
            get(token[1],specs, fp);
    }
    //prints the number of specified bytes starting from the start byte from the given filename
    else if(strcmp(token[0],"read")==0)
    {
        if(token[3]==NULL)
            printf("Please specify the number of bytes\n");
        else if(token[2]==NULL)
            printf("Please specify the starting byte\n");
        else if(token[1]==NULL)
            printf("Please give a filename\n");
        else
        {
            fileRead(atoi(token[2]), atoi(token[3]),token[1], fp, specs);
        }

    }
	//calls the function to print the volume name placed into the specs struct by getInfo()
    else if(strcmp(token[0],"volume")==0)
    {
	printVol(specs);
    }
    else if(strcmp(token[0], "ls")==0)
    {
	ls();
    }

    else if(strcmp(token[0], "cd")==0)
    {
	int index, offset, j;
	if(token[1] != NULL)
	{			//grab all listed folders
	    char * working_token = (char *) strtok(token[1], "/");
	    while(working_token != NULL)
	    {
		//parent directory
		if(strcmp(working_token, "..")==0)
		{
		    if(dir[1].DIR_Attr == 0x10 && dir[1].DIR_FirstClusterLow != 0)
			offset = LBAToOffset(dir[1].DIR_FirstClusterLow, specs);
		    else
			offset = (specs->BPB_NumFats * specs->BPB_FATSz32 * specs->BPB_BytsPerSec) + (specs->BPB_RsvdSecCnt * specs->BPB_BytsPerSec);
		}
		//this directory
		else if(!strcmp(working_token, "."))
		    continue;
		//find new directory
		else
		{
		    index = findFile(working_token);
		    if (index>0)
			offset = LBAToOffset(dir[index].DIR_FirstClusterLow, specs);
		    else
			printf("Error: Specified folder or path not found in file system\n");
		}
		//if found, go to cluster of this directory
		fseek(fp, offset, SEEK_SET);
                for(j=0; j<16; j++)
		{
		    //empty the previously built directory array
		    memset(&dir[j].DIR_Name,0,32);
		    //fill directory array based on contents of new working directory
		    fread(&dir[j],sizeof(struct DirectoryEntry),1,fp);
		    dir[j].DIR_Name[12] = '\0';
		}
		//move to next file/directory in path
		working_token = strtok(NULL,"/");
	    };
	}
	else
	    printf("Not a valid folder or path.\n");
    }
    free( working_root );

  }

  free(specs);
  fclose(fp);
  return 0;

}


//takes the file pointer as a parameter and structure to hold the specs
//stores the bytes per sector, sectors per cluster, Reserved sector count, number of fats
//and the fat size in the structure
void getInfo(FILE *fp, struct fatSpec* specs)
{
    int16_t BPB_BytsPerSec;
    int8_t BPB_SecPerClus;
    int16_t BPB_RsvdSecCnt;
    int8_t BPB_NumFats;
    int32_t BPB_FATSz32;
    char Vol_name[11];

    //for each variable, sets fp pointer to the appropriate byte in the file according to the
    //spec, reads the file
    fseek(fp, 11, SEEK_SET);
    fread(&(specs->BPB_BytsPerSec), sizeof(int16_t), 1,fp);


    fseek(fp, 13, SEEK_SET);
    fread(&(specs->BPB_SecPerClus), sizeof(int8_t), 1, fp);


    fseek(fp,14, SEEK_SET );
    fread(&(specs->BPB_RsvdSecCnt), sizeof(int16_t), 1, fp);


    fseek(fp, 16, SEEK_SET);
    fread(&(specs->BPB_NumFats), sizeof(int8_t), 1, fp);


    fseek(fp, 36, SEEK_SET);
    fread(&(specs->BPB_FATSz32), sizeof(int32_t), 1, fp);
    fseek(fp, 71, SEEK_SET);
    fread(&(specs->VOL_Name), 11, 1, fp);

}

//given the structure that holds the specs and prints them out
void printInfo(struct fatSpec* specs)
{

    printf("BPB_BytesPerSec: %d, %x\n",specs->BPB_BytsPerSec, specs->BPB_BytsPerSec);
    printf("BPB_SecPerClus: %d, %x\n",specs->BPB_SecPerClus, specs->BPB_SecPerClus);
    printf("BPB_RsvdSecCnt: %d, %x\n",specs->BPB_RsvdSecCnt, specs->BPB_RsvdSecCnt);
    printf("BPB_NumFats: %d, %x\n",specs->BPB_NumFats, specs->BPB_NumFats);
    printf("BPB_FATSz32: %d, %x\n", specs->BPB_FATSz32, specs->BPB_FATSz32);
}

//given the specs structure, prints out the Volume Name if there is one
void printVol(struct fatSpec* specs)
{
    printf("Volume Name: %s\n", specs->VOL_Name);
}

//get a pointer to a string and changes all the letters to lowercase
void stringToLower(char* str)
{
    int i;
    for(i = 0;i<strlen(str);i++)
        str[i] = tolower(str[i]);


}

//has two file names as the input
//compares the strings to determine if they are the same ignoring case
//and the period and spaces in the filename
//return 1 if they are the same, returns 0 if not
int fileNameCmp(char input[], char str[])
{
    int inputlen = strlen(input);
    int i = 0, j = 0;

    for(i;i<inputlen;i++)
    {
        if(input[i]=='.')
        {
            while(str[j]==' ')
                j++;
            j--;
        }
        else if(input[i]!=str[j])
        {
            return 0;
        }

        j++;
    }

    return 1;

}

//gets the file pointer and the spec structure pointer and populates the home directory
void popRootDir(FILE *fp, struct fatSpec* specs)
{
    int rootAddress = (specs->BPB_NumFats*specs->BPB_FATSz32*specs->BPB_BytsPerSec)
                +(specs->BPB_RsvdSecCnt*specs->BPB_BytsPerSec);

    fseek(fp, rootAddress, SEEK_SET);
    int i;

    //populates the directory
    for(i=0;i<16;i++)
    {

        fread(&dir[i],sizeof(struct DirectoryEntry),1,fp);

        dir[i].DIR_Name[12] = '\0';

        //changes the file name to lowercase to be case insensitive
        char* dirName = dir[i].DIR_Name;
        stringToLower(dirName);



    }

}

//get a file  name, checks if it is in the home directory
//then prints the attributes of the file
void stat(char input[])
{
    int i = 0;

    //no file name given
    if(input == NULL)
    {
        printf("Error: File not found\n");
        return;
    }

    //changes the given file name to lowercase to be case insensitive
    char* tok = input;
    stringToLower(tok);

    //searches the structure for the file
    while(fileNameCmp(input,dir[i].DIR_Name)==0&&i<16)
    {
        i++;
    }

    //prints the attributes if the file exists
    if(i==16) printf("Error: File not found\n");
    else
    {
        printf("attribute %#02x\n",dir[i].DIR_Attr);
        printf("Starting Cluster Number: %u\n",dir[i].DIR_FirstClusterLow);
        printf("File size: %"PRIu16"\n",dir[i].DIR_FileSize);
    }
}

//parameters are a filename, a fat spec struct and the file pointer to the fat32 image
//if found, copies the file to put it in the working directory
void get(char input[], struct fatSpec* specs, FILE* fp)
{
    //seraches for the file in the current directory
    int i = 0;
    while(fileNameCmp(input,dir[i].DIR_Name)==0&&i<16) i++;
    if(i>=16)
        printf("Error: file not found\n");
    else
    {

        //the file to be written to, given the name of the file it is copied from
        writeFile = fopen(input,"w");
        int32_t cluster = dir[i].DIR_FirstClusterLow;
        int clusSize = (specs->BPB_SecPerClus*specs->BPB_BytsPerSec);


        while(cluster!=-1)
        {
            //sets the file pointer the offset based on the current sector number
            char data[clusSize];
            int address = LBAToOffset(cluster, specs);
            fseek(fp,address,SEEK_SET );

            //read the data into a variable them writes it to the file
            fread(data, sizeof(data), 1, fp);

            fwrite(data,1, sizeof(data), writeFile);
            cluster = NextLB(cluster, specs, fp);
        }

        fclose(writeFile);

    }
}


//gives location of block of data given the sector
int LBAToOffset(int32_t sector, struct fatSpec* specs)
{
    return ((sector-2) * specs->BPB_BytsPerSec) + (specs->BPB_BytsPerSec * specs->BPB_RsvdSecCnt)
        + (specs->BPB_NumFats * specs->BPB_FATSz32 * specs->BPB_BytsPerSec);
}
//gives the address of the next block of data given a sector
int16_t NextLB(uint32_t sector, struct fatSpec* specs, FILE* fp)
{
    uint32_t FATAddress = (specs->BPB_BytsPerSec * specs->BPB_RsvdSecCnt) + (sector*4);
    uint16_t val;
    fseek(fp, FATAddress, SEEK_SET);
    fread (&val, 2, 1, fp);
    return val;
}


void ls()
{
    int i;

    //populates the home directory
    for(i=0;i<16;i++)
    {

        //changes the file name to lowercase to be case insensitive
        char* dirName = dir[i].DIR_Name;
        stringToLower(dirName);
	//checks for contents of directory array that ARE valid, non-deleted files/directories
	if ((dir[i].DIR_Attr == 0x01 || dir[i].DIR_Attr == 0x10 || dir[i].DIR_Attr == 0x20 || dir[i].DIR_Attr == 0x30) && dir[i].DIR_Name[0] != 0xffffffe5)
		//output detailed information of each file:
		//cluster, size, type, and name (similar to linux bash ls -l (ll)
        	printf("%d\t%d\t%x\t%s\n",dir[i].DIR_FirstClusterLow, dir[i].DIR_FileSize, dir[i].DIR_Attr, dir[i].DIR_Name);

    }
}

//function to find an input file within the directory array
int findFile(char * operand)
{
    int k;
    char f_name[12], i_name[12];
    memset(f_name, 32, 11);
    f_name[11] = 0;
    i_name[11] = 0;
    char* find = (char *) strtok(operand, ".");
    strncpy(f_name, find, strlen(find));
    find = strtok(NULL, ".");
    if(find != NULL)
	strcpy(&f_name[8], find);
    for(k = 0; k<16; k++)
    {
	memset(i_name, 32, 10);
	strncpy(i_name, dir[k].DIR_Name,11);
	if(!strncasecmp(f_name, i_name, 11))
		return k;
    }
    return -1;
}

void fileRead(int startByte, int numBytes, char filename[], FILE* fp, struct fatSpec* specs)
{

    //searches for the file, if not found, return
    int i = 0;

    while(fileNameCmp(filename,dir[i].DIR_Name)==0&&i<16)
    {
        i++;
    }


    if(i==16)
    {
        printf("Error: File not found\n");
        return;
    }

    //string to hold the data to be printed
    char data[specs->BPB_BytsPerSec];

    int secSize = specs->BPB_BytsPerSec;

    //set the file pointer to the beginning of where we want to start reading
    int16_t startClus = dir[i].DIR_FirstClusterLow;
    int start = startByte;
    if(start>secSize)
        while(start>secSize)
        {
            startClus = NextLB(startClus, specs, fp);
            start = start - secSize;
        }

    int16_t nextBlock;
    int offset = LBAToOffset(startClus, specs) + start;

    fseek(fp, offset, SEEK_SET);

    //the bytes we want to read do not span more than one cluster
    if(numBytes<secSize-start)
    {
        fread(data,numBytes , 1, fp);

        data[numBytes] = '\0';
        printf(data);
        printf("\n");
        return;
    }

    //the bytes we want to read do span more than one cluster
    //read the first bytes before the end of the first cluster
    numBytes = numBytes - secSize + startByte;
    fread(data,secSize - startByte , 1, fp);
    data[secSize - startByte] = '\0';
    printf(data);

    nextBlock = NextLB(startClus, specs, fp);
    offset = LBAToOffset(nextBlock, specs);
    fseek(fp, offset, SEEK_SET);


    //reads the bytes that span full clusters
    while(numBytes>secSize)
    {


        fread(data,secSize, 1, fp);
        data[secSize] = '\0';
        printf(data);
        numBytes = numBytes - secSize;

        //move fp to the next block of data
        nextBlock = NextLB(nextBlock, specs, fp);
        offset = LBAToOffset(nextBlock, specs);
        fseek(fp, offset, SEEK_SET);

    }

    //reads the remaining bytes in the last cluster
    fread(data,numBytes , 1, fp);
    data[numBytes] = '\0';
    printf(data);

    printf("\n");


}
