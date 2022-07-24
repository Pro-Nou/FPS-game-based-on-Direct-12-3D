#pragma once
#include <stdio.h>
class Read_OBJ
{
public:
	Read_OBJ();
	~Read_OBJ();
	bool ONCreat(char*filename);
	bool ONDestroy(void);
	int f = 0;
	int pf = 0;
	int v = 0;
	int vt = 0;
	int vn = 0;
	int pf1 = 0;
	int v1 = 0;
	int vt1 = 0;
	int vn1 = 0;
	int f1 = 0;
	float* vL;
	float* vtL;
	float* vnL;
	int* pfL;
	FILE * pFile;
	long lSize;
	char* buffer;
	size_t result;
};

