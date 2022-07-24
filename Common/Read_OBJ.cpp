#include <stdio.h>
#include "Read_OBJ.h"
#include <conio.h>
#include <stdlib.h>

Read_OBJ::Read_OBJ()
{
}


Read_OBJ::~Read_OBJ()
{
}

bool Read_OBJ::ONCreat(char * filename)
{
	fopen_s(&pFile, filename, "r");
	if (pFile == NULL)
	{
		exit(0);
	}
	fseek(pFile, 0, SEEK_END);
	lSize = ftell(pFile);
	rewind(pFile);
	buffer = (char*)malloc(sizeof(char)*lSize);
	if (buffer == NULL)
	{
		exit(0);
	}
	result = fread(buffer, 1, lSize, pFile);
	/*if (result != lSize)
	{
		exit(0);
	}*/
	fclose(pFile);
	for (int i = 0; i < result; i++)
	{
		if (buffer[i] == 'f'&&buffer[i + 1] == ' ')
			f++;
		else if (buffer[i] == '/')
			pf++;
		else if (buffer[i] == 'v'&&buffer[i + 1] == ' ')
			v++;
		else if (buffer[i] == 'v'&&buffer[i + 1] == 't'&&buffer[i + 2] == ' ')
			vt++;
		else if (buffer[i] == 'v'&&buffer[i + 1] == 'n'&&buffer[i + 2] == ' ')
			vn++;
	}
	vL = (float*)malloc(sizeof(float) * v * 3);
	vtL = (float*)malloc(sizeof(float) * vt * 2);
	vnL = (float*)malloc(sizeof(float) * vn * 3);
	pfL = (int*)malloc(sizeof(int) * (f + 3 * pf / 2));
	for (int i = 0; i < result; i++)
	{
		if(buffer[i]=='#')
		{
			while (buffer[i] != '\n')
				i++;
		}
		if (buffer[i] == 'v'&&buffer[i + 1] == ' ')
		{
			char num[100];
			int numl = 0;
			int time = 0;
			for (i = i + 2; buffer[i] != '\n'; i++)
			{
				if ((buffer[i] >= '0'&&buffer[i] <= '9') || buffer[i] == '.' || buffer[i] == '-')
				{
					num[numl] = buffer[i];
					numl++;
				}
				else
				{
					num[numl] = '\0';
					vL[v1] = (float)atof(num);
					v1++;
					numl = 0;
				}
			}
			num[numl] = '\0';
			vL[v1] = (float)atof(num);
			v1++;
			numl = 0;
		}
		else if (buffer[i] == 'v'&&buffer[i + 1] == 't'&&buffer[i + 2] == ' ')
		{
			char num[100];
			int numl = 0;
			int time = 0;
			for (i = i + 3; buffer[i] != '\n'; i++)
			{
				if ((buffer[i] >= '0'&&buffer[i] <= '9') || buffer[i] == '.' || buffer[i] == '-')
				{
					num[numl] = buffer[i];
					numl++;
				}
				else
				{
					num[numl] = '\0';
					vtL[vt1] = (float)atof(num);
					vt1++;
					numl = 0;
					time++;
				}
			}
			num[numl] = '\0';
			vtL[vt1] = (float)atof(num);
			vt1++;
			numl = 0;
		}
		else if (buffer[i] == 'v'&&buffer[i + 1] == 'n'&&buffer[i + 2] == ' ')
		{
			char num[100];
			int numl = 0;
			int time = 0;
			for (i = i + 3; buffer[i] != '\n'; i++)
			{
				if ((buffer[i] >= '0'&&buffer[i] <= '9') || buffer[i] == '.' || buffer[i] == '-')
				{
					num[numl] = buffer[i];
					numl++;
				}
				else
				{
					num[numl] = '\0';
					vnL[vn1] = (float)atof(num);
					vn1++;
					numl = 0;
				}
			}
			num[numl] = '\0';
			vnL[vn1] = (float)atof(num);
			vn1++;
			numl = 0;
		}
		else if (buffer[i] == 'f'&&buffer[i + 1] == ' ')
		{
			char num[100];
			int numl = 0;
			int t = 0;
			for (i = i + 2; buffer[i]!='\n'; i++)
			{
				if (buffer[i] >= '0'&&buffer[i] <= '9')
				{
					num[numl] = buffer[i];
					numl++;
				}
				else
				{
					num[numl] = '\0';
					pfL[pf1] = atoi(num);
					pfL[pf1]--;
					numl = 0;
					pf1++;
				}
			}
			num[numl] = '\0';
			pfL[pf1] = atoi(num);
			pfL[pf1]--;
			numl = 0;
			pf1++;
			pfL[pf1] = -1;
			pf1++;
		}
		else
		{
			while (buffer[i] != '\n')
				i++;
		}
	}
	return true;
}

bool Read_OBJ::ONDestroy(void)
{
	free(buffer);
	return true;
}
