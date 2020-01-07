.PHONY: all
all: usecases

usecases: DataGenerator WorkerApp DataDistributor
	@echo "build of usecases executables is complete"

CCFLAGS = $(CFLAGS) -std=c++14

# Data generator obj
dg_objects = DataGenerator.o OptParams.o PipeStruct.o SimulatedMsg.o Stats.o FPSThread.o DoSleep.o GenerateData.o
# WorkerApp obj
wa_objects = WorkerApp.o PipeStruct.o SimulatedMsg.o Stats.o
# =copy of the binary
DataDistributor: DataGenerator
	cp -f DataGenerator DataDistributor

%.o:%.cpp  
	echo "=======================" | tee -a make.out
	g++ $(CCFLAGS) -c $< 2>&1 | tee -a make.out

DataGenerator: ${dg_objects} Makefile
	g++ $(CCFLAGS) -o $@ ${dg_objects} 2>&1 | tee -a make.out

WorkerApp: ${wa_objects} Makefile
	g++ $(CCFLAGS) -o $@ ${wa_objects} 2>&1 | tee -a make.out 

WorkerAppTmp: WorkerAppTmp.o PipeStruct.o SimulatedMsg.o Stats.o 
	g++ $(CCFLAGS) -o $@ $< 2>&1 | tee -a make.out 

test: test.cpp 
	g++ -g -std=c++14 test.cpp -pthread  -o test 

testOne: testOne.o Stats.o
	g++ $(CCFLAGS) -o $@ $^ 2>&1 | tee make.out

dgl_objects = DataGenerator.lockall.o OptParams.o PipeStruct.o SimulatedMsg.o Stats.o FPSThread.o DoSleep.o

DataGenerator.lockall.o: DataGenerator.cpp DataGenerator.h PipeStruct.h OptParams.h SimulatedMsg.h DoSleep.h FPSThread.h
	echo "=======================" | tee -a make.out
	g++ $(CCFLAGS) -DLOCKALL -c $< -o DataGenerator.lockall.o 2>&1 | tee -a make.out

DataGenerator.lockall : ${dgl_objects} Makefile
	g++ $(CCFLAGS) -o $@ ${dgl_objects} 2>&1 | tee -a make.out

CLEANUP += DataGenerator WorkerApp DataDistributor make.out

