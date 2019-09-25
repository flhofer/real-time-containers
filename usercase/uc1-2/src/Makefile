usecases: DataGenerator WorkerApp DataDistributor
	@echo "build of usecases executables is complete"

CCFLAGS = $(CFLAGS) -g -std=c++14 -pthread

dg_objects = DataGenerator.o OptParams.o PipeStruct.o SimulatedMsg.o Stats.o FPSThread.o DoSleep.o GenerateData.o

wa_objects = WorkerApp.o PipeStruct.o SimulatedMsg.o Stats.o

DataDistributor: DataGenerator
	cp -f DataGenerator DataDistributor

DataGenerator.o: DataGenerator.cpp DataGenerator.h PipeStruct.h OptParams.h SimulatedMsg.h DoSleep.h FPSThread.h

GenerateData.o: DataGenerator.h GenerateData.h PipeStruct.h OptParams.h SimulatedMsg.h DoSleep.h FPSThread.h

SimulatedMsg.o: SimulatedMsg.cpp Stats.h

PipeStruct.o: PipeStruct.cpp PipeStruct.h

Stats.o: Stats.cpp Stats.h

OptParams.o: OptParams.cpp OptParams.h

FPSThread.o: FPSThread.cpp FPSThread.h OptParams.h DataGenerator.h

DoSleep.o: DoSleep.cpp DoSleep.h

%.o:%.cpp  
	echo "=======================" | tee -a make.out
	g++ $(CCFLAGS) -c $< 2>&1 | tee -a make.out

DataGenerator: ${dg_objects} Makefile
	g++ $(CCFLAGS) -o $@ ${dg_objects} 2>&1 | tee -a make.out

WorkerApp: ${wa_objects} Makefile
	g++ $(CCFLAGS) -o $@ ${wa_objects} 2>&1 | tee -a make.out 

WorkerAppTmp: WorkerAppTmp.o PipeStruct.o SimulatedMsg.o Stats.o 
	g++ $(CCFLAGS) -o $@ WorkerAppTmp.o PipeStruct.o SimulatedMsg.o Stats.o 2>&1 | tee -a make.out 

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

