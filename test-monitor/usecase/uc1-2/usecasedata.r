# Set the working directory
setwd("./logs/")
library(ggplot2)
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
	plot  <- list("Test" = -1, "FPS"= -1, "dataFPS"=NULL, "dataT"=NULL)
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

plotData<-function(directory, time) {
	write(paste0("Proceeding with directory ", directory), stderr())

	# init
	gplot <- ggplot(mapping= aes(x=bStart, y=Count)) 
	xmin <- 250
	xmax <- 0
	fpsmin <- 250
	fpsmax <- 0
	files <- dir(directory, pattern= "(^workerapp[0-9](\\.|-fifo)*log)")

	# Combine results of test batches
	for (f in seq(along=files)){

		histLoad<-loadData(paste0(directory, "/", files[f]))

		write(paste0("Size of dataset for ", files[f], " iter ", f, " :", length(histLoad)), stderr())

		k <- 1

		for (k in 1:length(histLoad)) {
			xmin <- min(xmin, histLoad[[k]]$dataT$rows$bStart)
			xmax <- max(xmax, histLoad[[k]]$dataT$rows$bStart)

			fpsmin <- min(fpsmin, histLoad[[k]]$dataFPS$min)
			fpsmax <- max(fpsmax, histLoad[[k]]$dataFPS$max)

			write(paste0(fpsmin ," - ", fpsmax), stderr())

			if (time) {
				gplot <- gplot + geom_bar(data = histLoad[[k]]$dataT$rows,stat="identity", width = 0.1) # width=histLoad[[1]]$dataT$res) 
			}
			else {
				gplot <- gplot + geom_bar(data = histLoad[[k]]$dataFPS$rows,stat="identity", width = 0.1) # width=histLoad[[1]]$dataT$res) 
			}
		}
	}	  

	directory <- (gsub("/", "_", directory))
	pdf(file= paste0(directory,".pdf"), width = 10, height = 10)
	gplot <- gplot + labs(x='occurrence of exeution value')

	if (time) {
		gplot <- gplot + scale_x_continuous(trans='log10',limits=c(10,50000)) 
	}
	else {
		gplot <- gplot +  xlim(c(6,10))
	}

	print (gplot)
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
	plot  <- list("nPeriods" = -1, "nOverruns"= -1, "nViolations"=-1, "data"=NULL)
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
	plot$nPeriods <- as.double(stringValue(linn[act])[4])
	plot$nOverruns <- as.double(stringValue(linn[act+1])[3])
	plot$nViolations <- as.double(stringValue(linn[act+2])[3])

		# create data structure for histogram
	plot$data<- list("run" = -1)

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
	plot$data <- data.frame ("run"=records$V1)
			   
	# compare min max avg
	min = min(plot$data$run)
	max = max(plot$data$run)
	avg = mean(plot$data$run)	

	write(paste0(fName, ";", min, ";", max, ";", avg), stdout())

	return(plot)
}

readDeadline<-function(directory) {
	write(paste0("Deadline read directory ", directory), stderr())

	# init
	gplot <- ggplot(mapping=aes(x=run)) 
	files <- dir(directory, pattern= "(^workerapp[0-9]-deadline.*log)")

	# Combine results of test batches
	for (f in seq(along=files)){

		delays<-loadDelays(paste0(directory, "/", files[f]))

		if (length(delays$data) > 0) {

			gplot <- gplot + geom_histogram(data = delays$data, fill=col[f]) 

		}
	}	  

	directory <- (gsub("/", "_", directory))
	pdf(file= paste0(directory,"delay.pdf"), width = 10, height = 10)
	gplot <- gplot + labs(x='occurrence of exeution value') 
#		     scale_x_continuous(trans='log10',limits=c(10,50000)) 
#			 xlim(c(7,9)) 

	print (gplot)
	dev.off()
}

######### Start script main ()

## sink to write worst performing thread to a csv
sink("stats-maxmin.csv")

write("FileName;worst min; - max; - avg", stdout())

col<- viridis(8)

df <- df <- file.info(dir(".", pattern= "(^UC1|TEST[0-9]_1$)"))
write(rownames(df)[which.max(df$mtime)], stderr())

df <- df <- file.info(dir(".", pattern= "(^UC2|TEST[0-9]_2$)"))
write(rownames(df)[which.max(df$mtime)], stderr())

# Find all directories with pattern.. UC1
dirs <- dir(".", pattern= "(^UC1|TEST[0-9]_1$)")

# Combine results of test batches
for (d in seq(along=dirs)){
	plotData(dirs[d], 0)
}

# Find all directories with pattern.. UC1
dirs <- dir(".", pattern= "(^UC2|TEST[0-9]_2$)")

# Combine results of test batches
for (d in seq(along=dirs)){
	# Find all directories with pattern.. Test
	dirs2 <- dir(dirs[d], pattern= "^Test")

	# Combine results of test batches
	for (e in seq(along=dirs2)){
		plotData(paste0(dirs[d], "/",dirs2[e]), 1)
		readDeadline(paste0(dirs[d], "/",dirs2[e]))
	}
}

## back to the console
sink(type="output")

