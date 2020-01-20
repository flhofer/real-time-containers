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
	print(head(linn))

	#rep_date_entries = grep("^ ", linn)
	#print(head(rep_date_entries))
	#linn <- linn[-rep_date_entries]
	#print(head(linn))

	# Read text file remove everything except numbers

	#linn <- (gsub("[a-zA-Z:()>=<]", "", linn))


	# init values
	start=1
	tstart=1
	tend=length(linn)

	while (start < length(linn)){
		for (i in start:length(linn)){
			while(length(linn) >0 && !startsWith(linn[1], 'Test'))
				linn<-linn[-1]

			line <- strsplit(linn[1], " ")
			print(line)

			endline=2

			while(length(linn) >0 && !startsWith(linn[1], 'Histogram'))
				linn<-linn[-1]
			# TODO: parse hstrogram values

			while(length(linn) >0 && !startsWith(linn[endline], 'EndTime'))
				endline=endline+1

			# Transform into table, filter and add names
			records = read.table(text=linn[2:(endline-2)])
			print(head(records))
					   
		}
		# once for is finished, increase start
		start=i+1
	}
	

	# Transform into table, filter and add names
	records = read.table(file= fName, skip=31)
	head(records)
	records <- data.frame ("RunT"=records$V3,"Period"=records$V4,"rStart"=records$V7, "cDur"=records$V9)

	return(records)
}

data<-loadData('workerapp0.log')

print(data)

