usecases: DataGenerator WorkerApp DataDistributor
	@echo "build of usecases executables is complete"

OBJDIR ?= .
CCPFLAGS += -g -std=c++14 -pthread
CLEANUP += DataGenerator WorkerApp DataDistributor make.out

# Data generator obj
dg_objects = $(addprefix $(OBJDIR)/,DataGenerator.o OptParams.o PipeStruct.o SimulatedMsg.o Stats.o FPSThread.o DoSleep.o GenerateData.o)
# WorkerApp obj
wa_objects = $(addprefix $(OBJDIR)/,WorkerApp.o PipeStruct.o SimulatedMsg.o Stats.o)
# =copy of the binary
DataDistributor: DataGenerator
	cp -f DataGenerator DataDistributor

$(OBJDIR)/%.o:%.cpp  
	echo "=======================" | tee -a make.out
	g++ $(CCPFLAGS) -c -o $@ $< 2>&1 | tee -a make.out

DataGenerator: ${dg_objects}
	g++ $(CCPFLAGS) -o $@ ${dg_objects} 2>&1 | tee -a make.out

WorkerApp: ${wa_objects}
	g++ $(CCPFLAGS) -o $@ ${wa_objects} 2>&1 | tee -a make.out 

# Tests only

WorkerAppTmp: $(addprefix $(OBJDIR)/,WorkerAppTmp.o PipeStruct.o SimulatedMsg.o Stats.o) 
	g++ $(CCPFLAGS) -o $@ $(addprefix $(OBJDIR)/,WorkerAppTmp.o PipeStruct.o SimulatedMsg.o Stats.o) 2>&1 | tee -a make.out 

testUC: test.cpp 
	g++ -g -std=c++14 test.cpp -pthread  -o test 

testUCOne: testOne.o Stats.o
	g++ $(CCPFLAGS) -o $@ $^ 2>&1 | tee make.out

dgl_objects = DataGenerator.lockall.o OptParams.o PipeStruct.o SimulatedMsg.o Stats.o FPSThread.o DoSleep.o

DataGenerator.lockall.o: DataGenerator.cpp DataGenerator.h PipeStruct.h OptParams.h SimulatedMsg.h DoSleep.h FPSThread.h
	echo "=======================" | tee -a make.out
	g++ $(CCPFLAGS) -DLOCKALL -c $< -o DataGenerator.lockall.o 2>&1 | tee -a make.out

DataGenerator.lockall : ${dgl_objects}
	g++ $(CCPFLAGS) -o $@ ${dgl_objects} 2>&1 | tee -a make.out
