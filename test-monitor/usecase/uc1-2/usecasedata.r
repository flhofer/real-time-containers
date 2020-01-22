# Set the working directory
setwd("./log/UC1.20200116/") # temp

stringValue <-function(line) {
			return(unlist(strsplit(r<-gsub('[():=,]' ,'', line), " ")))
	}

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	# cat (fName, "\n")

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

#				print(plot)
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

			# create data structure for histogram
			dataD<- list("min" = -1, "max" = -1, "avg" = -1, "unit" = "",
					"rows"=NULL, "expMin"=-1, "expMax"=1, "bins"=-1, "res"=-1)

			# parse min max avg
			dataD$min <- stringValue(linn[act+3])[2]
			dataD$max <- stringValue(linn[act+4])[2]
			dataD$avg <- stringValue(linn[act+5])[2]
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

	return(plots)

}

histLoad<-loadData('workerapp0.log')

cat("Size of dataset:", length(histLoad))


