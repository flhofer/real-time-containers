# Set the working directory
setwd("./log/UC1.20200116/") # temp

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
	start=1

	while (start < length(linn)){
			while(length(linn) > start && !startsWith(linn[start], 'Test'))
				start=start+1
			print(head(linn[start]))

			if (length(linn) <= start) 
				break

			# parse header data
			line <- strsplit(r<-gsub('[():=]' ,'', linn[1]), " ")
			print(line)

			# find beginning of table
			while(length(linn) > act && !startsWith(linn[act], 'Histogram'))
				act=act+1
			# TODO: parse hstrogram values

			# find end of this table of values
			while(length(linn) > act && !startsWith(linn[act], 'EndTime'))
				act=act+1

			#counters
			tstart=start+1
			tend=act-2

			print(head(linn[tstart]))

			# Transform into table, filter and add names
			records = read.table(text= (gsub('[^0-9. ]' ,'', linn[tstart:tend])), header=FALSE)
			print(head(records))
			records <- data.frame ("Count"=records$V1,"bStart"=records$V2,"bEnd"=records$V3)
			print(head(records))
					   
		# once for is finished, increase start
		start=tend+1
	}
	
	return(records)
}

data<-loadData('workerapp0.log')

print(data)

