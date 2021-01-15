# Set the working directory
setwd("./logs/")
library(ggplot2)
library(dplyr)
library(viridis)

stringValue <-function(line) {
			return(unlist(strsplit(r<-gsub('[():=,]' ,'', line), " ")))
	}

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	#write(fName, stderr())

	if (!file.exists(fName) || file.info(fName)$size == 0) {
		# break here if file does not exist
		records <- data.frame ()
		return(records)
	}
	#TODO: add protection big size


	# read string buffer
	linn <- readLines(fName)
	#print(head(linn))

	# init values
	start <- 1
	plots <- list()
	plot  <- list(name=fName, "Test" = -1, "FPS"= -1, "dataFPS"=NULL, "dataT"=NULL)
	min=9999999
	max = -1
	avg = -1
	k <- 1
	while (start < length(linn)){
			# find next starting test entry
			while(length(linn) > start && !startsWith(linn[start], 'Test'))
				start <- start+1

			# end of parsing buffer?
			if (length(linn) <= start) 
				break

			# save to the actual parsing position variable
			act=start

			# parse header data, ex `Test 0 (24 FPS):`
			line <- stringValue(linn[act]) 

#			print(plot$Test)

			# Do we have a new test number or just further data?
			if (( (as.integer(line[2]) != plot$Test) || (as.integer(line[3]) != plot$FPS)) &&
				(plot$Test > -1)) {

				# new test data, append existing results to list, use test number for index
				plots[[k]] <- plot
				k <- k + 1
				# reset test data
				plot <-list("Test" = as.integer(line[2]), "FPS"= as.integer(line[3]),
						 "dataFPS"=NULL, "dataT"=NULL)

			}
			else 
				if (plot$Test == -1) {
				#Update values in the new initialized list
				plot$Test <- as.integer(line[2])		# second element = testno (offset 1)
				plot$FPS  <- as.integer(line[3])		# third element = number in parentheses 
				}

			# parse header data, ex `Test 0 (24 FPS):`
			line <- stringValue(linn[act+1]) 

			## check for Stat print start, otherwise no data
			if ( !identical(line[1], "**")) {
				#print(paste0("Warn, empty stats in ", fName))
				return()
			}

			# create data structure for histogram
			dataD<- list("min" = -1, "max" = -1, "avg" = -1, "unit" = "",
					"rows"=NULL, "expMin"=-1, "expMax"=1, "bins"=-1, "res"=-1)

			# parse min max avg
			dataD$min <- as.double(stringValue(linn[act+3])[2])
			dataD$max <- as.double(stringValue(linn[act+4])[2])
			dataD$avg <- as.double(stringValue(linn[act+5])[2])
			act <- act +6		

			# find beginning of table
			while(length(linn) > act && !startsWith(linn[act], 'Histogram'))
				act=act+1

			line <- stringValue(linn[act])
			dataD$expMin <- as.double(line[2])
			dataD$expMax <- as.double(line[4])
			dataD$bins <- as.integer(line[6])
			dataD$res <- as.double(line[11])
			dataD$unit <- line[12]
			
			tstart=act+1

			# parse hstrogram values..

			# find end of this table of values
			while(length(linn) > act && !startsWith(linn[act], 'EndTime'))
				act=act+1

			#counters
			tend=act-2

			# Transform into table, filter and add names
			records = read.table(text= (gsub('[^0-9. ]' ,'', linn[tstart:tend])), header=FALSE)
			dataD$rows <- data.frame ("Count"=records$V1,"bStart"=records$V2,"bEnd"=records$V3)
					   
			# assign to set in data structure
			if (identical(dataD$unit, "FPS")) {
				plot$dataFPS <- dataD
				# compare min max avg
				min = min(min,dataD$min)
				max = max(max,dataD$max)
				avg = max(avg,dataD$avg)	
			}
			else {
				plot$dataT <- dataD
			}

		# once for is finished, increase start
		start=tend+1
	}

	# append last non-empty plot#
	if (plot$Test > -1) {
#		print(plot)
		plots[[k]] <- plot
	}

	write(paste0(fName, ";", min, ";", max, ";", avg), stdout())
	return(plots)

}

plotData<-function(directory) {
	write(paste0("Proceeding with directory ", directory), stderr())

	# detect files
	files <- dir(directory, pattern= "(^workerapp[0-9](\\.|-fifo)*log)")
	# Load all results into a list
	dataAll <- lapply (paste0(directory, "/", files), loadData)
	lapply(dataAll, function(d) write(paste0("Size of dataset for ", d[[1]]$name, " :", length(d)), stderr()))
	# Extract FPS part only for each test of each worker
	dataFPS <- lapply(dataAll, function(d) lapply(d, function(x) data.frame(dat=x$dataFPS$rows)))
	# Merge FPS parts of each worker
	dataFPS <- lapply(dataFPS, function(x) Reduce(function(...) merge(..., all=TRUE, sort=TRUE), x))
	# Aggregate sums FPS of workers
	#dataFPS <- lapply(dataFPS, function(x) aggregate(list(Count=x$dat.Count), by=list(bStart=x$dat.bStart, bEnd=x$dat.bEnd), sum))
	# merge worker tables
	dataFPS <- Reduce(function(...) merge(..., all=TRUE, sort=TRUE), dataFPS)
	# Aggregate sums FPS
	dataFPS <- aggregate(list(Count=dataFPS$dat.Count), by=list(bStart=dataFPS$dat.bStart, bEnd=dataFPS$dat.bEnd), sum)

	# Extract fDelay part only for each test of each worker
	dataT <- lapply(dataAll, function(d) lapply(d, function(x) data.frame(dat=x$dataT$rows)))
	# Merge fDelay parts of each worker
	dataT <- lapply(dataT, function(x) Reduce(function(...) merge(..., all=TRUE, sort=TRUE), x))
	# Aggregate sums fDelay of workers
	# <- lapply(dataT, function(x) aggregate(list(Count=x$dat.Count), by=list(bStart=x$dat.bStart, bEnd=x$dat.bEnd), sum))
	# merge worker tables
	dataT <- Reduce(function(...) merge(..., all=TRUE, sort=TRUE), dataT)
	# Aggregate sums fDelay
	dataT <- aggregate(list(Count=dataT$dat.Count), by=list(bStart=dataT$dat.bStart, bEnd=dataT$dat.bEnd), sum)

	directory <- (gsub("/", "_", directory))
	sz = sum(dataT$bStart>100000)
	png(file= paste0(directory,"fdelay.png"), width = 600, height = 500)
	ggp <- ggplot(mapping= aes(x=bStart, y=Count)) +
			geom_bar(data = dataT,stat="identity", width = 0.1) +
    		scale_fill_viridis_d() +
#	 		ylim (c(0,5000)) +
	 		scale_x_continuous(trans='log10',limits=c(10,100000)) +
			labs(x=expression(paste("Frame delivery delays [", mu, "s]")), y="Occurrence count", fill="Instance")
	if (sz){
		ggp <- ggp + geom_point(aes(y=0, x = 100000, size=sz), shape=17, , color="red", fill="red", show.legend=FALSE)
	}
	print(ggp)
	dev.off()

	sz = sum(dataFPS$bStart>10)
	png(file= paste0(directory,"FPS.png"), width = 600, height = 500)
	ggp <- ggplot(mapping= aes(x=bStart, y=Count)) +
		geom_bar(data = dataFPS,stat="identity", width = 0.1) +
		scale_fill_viridis_d() +
		xlim (c(6,10)) +
 		scale_y_log10() +
#		ylim (c(0,10000)) +
 		labs(x=paste("Processing frame rate, FPS"), y="Occurrence count", fill="Instance") 
	if (sz){
   		ggp <- ggp + geom_point(aes(y=0, x = 10, size=sz), shape=17, , color="red", fill="red", show.legend=FALSE)
	} 		
	print(ggp)
	dev.off()
}

loadDelays<-function(fName){
	#Function, load data from text file for average plots 

	# Read text into R
	#write(fName, stderr())

	if (!file.exists(fName) || file.info(fName)$size == 0) {
		# break here if file does not exist
		records <- data.frame ()
		return(records)
	}
	#TODO: add protection big size


	# read string buffer
	linn <- readLines(fName)
	#print(head(linn))

	# init values
	start <- 1
	plot  <- list("name"=fName, "nPeriods" = -1, "nOverruns"= -1, "nViolations"=-1, "dataD"=NULL)
	k <- 1

	# find starting test entry
	while(length(linn) > start && !startsWith(linn[start], 'Total Number'))
		start <- start+1

	# end of parsing buffer?
	if (length(linn) <= start) 
		break

	# save to the actual parsing position variable
	act=start

	# parse min max avg
	plot$nPeriods <- as.double(stringValue(linn[act])[5])
	plot$nOverruns <- as.double(stringValue(linn[act+1])[3])
	plot$nViolations <- as.double(stringValue(linn[act+2])[4])

		# create data structure for histogram
	plot$dataD<- list("run" = -1)

	act=act+4
	tstart=act

	# parse hstrogram values..

	# find end of this table of values
	while(length(linn) > act && !startsWith(linn[act], '------'))
		act=act+1

	#counters
	tend=act-1

	# Transform into table, filter and add names
	records = read.table(text= (gsub('[^0-9. ]' ,'', linn[tstart:tend])), header=FALSE)
	plot$dataD <- data.frame ("run"=records$V1)
			   
	# compare min max avg
	min = min(plot$dataD$run)
	max = max(plot$dataD$run)
	avg = mean(plot$dataD$run)	

	write(paste0(fName, ";", min, ";", max, ";", avg), stdout())

	return(plot)
}

readDeadline<-function(directory) {
	write(paste0("Deadline read directory ", directory), stderr())

	# detect files
	files <- dir(directory, pattern= "(^workerapp[0-9]-deadline.*log)")
	# Combine results of test batches
	delays <- lapply (paste0(directory, "/", files), loadDelays)
	# Create frames
	delays <- lapply( delays, function(x) data.frame(name=rep(x$name), dataD=x$dataD$run))
	# Merge
	delays <- Reduce(function(...) merge(..., all=T), delays)
	# filter name tags, reduce count in 1000ths
	delays[,1]<- gsub(".*(workerapp[0-9]).*", "\\1", delays[,1]) 
	delays[,2]<- delays[,2]/1000 

	directory <- (gsub("/", "_", directory))
	png(file= paste0(directory,"delay.png"), width = 700, height = 500)

	sz = sum(delays$dataD>3000)
	ggp <- ggplot(delays, aes(x=dataD)) +
		geom_histogram(aes(fill=name)) +
		labs(x=expression(paste("Periodic task runtime values [", mu, "s]")), y="Occurrence count", fill="Instance") +
		xlim (c(400,3000)) +
		ylim (c(0,500)) +
		scale_fill_viridis_d()
	if (sz){
   		ggp <- ggp + geom_point(aes(y=0, x = 3000, size=sz), shape=17, , color="red", fill="red", show.legend=FALSE)
	}
	print (ggp)
	dev.off()
}

######### Start script main ()

## sink to write worst performing thread to a csv
#sink("stats-maxmin.csv")

write("FileName;worst min; - max; - avg", stdout())

# df <- df <- file.info(dir(".", pattern= "(^UC1|TEST[0-9]_1$)"))
# write(rownames(df)[which.max(df$mtime)], stderr())

# df <- df <- file.info(dir(".", pattern= "(^UC2|TEST[0-9]_2$)"))
# write(rownames(df)[which.max(df$mtime)], stderr())

# Find all directories with pattern.. UC1
dirs <- dir(".", pattern= "(^UC1|TEST[0-9]_1$)")
# Combine results of test batches
sapply(dirs, function(x) plotData(x) )

# Find all directories with pattern.. UC2
dirs <- dir(".", pattern= "(^UC2|TEST[0-9]_2$)")
# Find all directories with pattern.. Test
dirs2 <- dir(dirs, pattern= "^Test")

# Combine results of test batches
sapply( paste0(dirs, "/", dirs2), function(x) plotData(x))
sapply( paste0(dirs, "/", dirs2), function (x) readDeadline(x))

# back to the console
sink(type="output")
warnings()

