// try2.0.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include "try2.0.h"
#include "function.h"
#include "FileSystem.h"

//全局变量定义
const int kSuperBlockStartAddress = 0;																				//超级块 偏移地址,占一个磁盘块
const int kInodeBitmapStartAddress = 1 * BLOCK_SIZE;																//inode位图 偏移地址，占512个磁盘块，最多监控 524288 个inode的状态
const int kBlockBitmapStartAddress = kInodeBitmapStartAddress + INODE_NUM * sizeof(bool) / BLOCK_SIZE * BLOCK_SIZE;	//block位图 偏移地址，占1024个磁盘块，最多监控1G的状态
const int kInodeStartAddress = kBlockBitmapStartAddress + BLOCK_NUM * sizeof(bool) / BLOCK_SIZE * BLOCK_SIZE;		//inode节点区 偏移地址，占 65536 个磁盘块
const int kBlockStartAddress = kInodeStartAddress + INODE_NUM * INODE_SIZE / BLOCK_SIZE * BLOCK_SIZE;				//block数据区 偏移地址 ，占 BLOCK_NUM 个磁盘块

const int kSumSize = kBlockStartAddress + BLOCK_NUM * BLOCK_SIZE;													//虚拟磁盘文件大小

//单个文件最大大小
const int kFileMaxSize = 10 * BLOCK_SIZE +																			//10个直接块
BLOCK_SIZE / sizeof(int) * BLOCK_SIZE +																				//一级间接块
(BLOCK_SIZE / sizeof(int))*(BLOCK_SIZE / sizeof(int)) * BLOCK_SIZE													//二级间接块
+ (BLOCK_SIZE / sizeof(int))*(BLOCK_SIZE / sizeof(int)) *(BLOCK_SIZE / sizeof(int))* BLOCK_SIZE;					//三级间接块

FILE* fw=nullptr;																										//虚拟磁盘文件 写文件指针
FILE* fr = nullptr;
int main()
{
	while (1)
	{
		std::string start_input_command;
		std::cin >> start_input_command;
		if (start_input_command == "create")
		{
			std::string system_name = start_input_command.substr(start_input_command.find(" ") + 1)+SYSTEMSUFFIX;
			fw = fopen(system_name.c_str(), "wb");																				//只写打开虚拟磁盘文件（二进制文件）
			if (fw == NULL) //打开文件失败
			{
				printf("虚拟磁盘文件打开失败\n");
				return 0;
			}
			fr = fopen(system_name.c_str(), "rb");
			FileSystem minifs(fw, fr, 1);
		}
		else if(start_input_command=="mount")
		{
			std::string system_name = start_input_command.substr(start_input_command.find(" ") + 1) + SYSTEMSUFFIX;
			fw = fopen(system_name.c_str(), "wb");																				//只写打开虚拟磁盘文件（二进制文件）
			if ((fr = fopen(system_name.c_str(), "rb")) == NULL) //打开文件失败
			{
				printf("没有当前系统\n");
				return 0;
			}
			fw = fopen(system_name.c_str(), "rb+");	//读写打开一个二进制文件，只允许读写数据。
			if (fw == NULL) 	//打开文件失败
			{
				printf("虚拟磁盘文件打开失败\n");
				return 0;
			}
			FileSystem minifs(fw, fr, 1);
			printf("           _       _       _____ _ _      ____            _                 \n");
			printf(" _ __ ___ (_)_ __ (_)     |  ___(_) | ___/ ___| _   _ ___| |_ ___ _ __ ___  \n");
			printf("| '_ ` _ \\| | '_ \\| |_____| |_  | | |/ _ \\___ \\| | | / __| __/ _ \\ '_ ` _ \\ \n");
			printf("| | | | | | | | | | |_____|  _| | | |  __/___) | |_| \\__ \\ | | __/ | | | | |\n");
			printf("|_| |_| |_|_|_| |_|_|     |_|   |_|_|\\___|____/ \\__, |___/\\__\\___|_| |_| |_|\n");
			printf("                                                |___/\n");
			while (1)
			{
				if (minifs.error_ != 0)
				{
					minifs.error_ = 0;
				}
				minifs.Parser();
			}
		}
		else
		{
			std::cout << "Wrong Command"<<std::endl;
		}
		fclose(fw);		//释放文件指针
		fclose(fr);		//释放文件指针
		return 0;
	}
}