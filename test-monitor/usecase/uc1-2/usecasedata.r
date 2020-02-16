# Set the working directory
setwd("./logs/") # temp
library(ggplot2)
library(viridis)

stringValue <-function(line) {
			return(unlist(strsplit(r<-gsub('[():=,]' ,'', line), " ")))
	}

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	#print(fName)

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
			dataD$min <- as.integer(stringValue(linn[act+3])[2])
			dataD$max <- as.integer(stringValue(linn[act+4])[2])
			dataD$avg <- as.integer(stringValue(linn[act+5])[2])
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
				# compare min max avg
				min = min(min,dataD$min)
				max = max(max,dataD$max)
				avg = max(avg,dataD$avg)	
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

	# init
	gplot <- ggplot(mapping= aes(x=bStart, y=Count)) 
	xmin <- 250
	xmax <- 0

	for (i in 0:7) {
		fName<- paste0(directory, '/workerapp', i,'.log')
		histLoad<-loadData(fName)

		write(paste0("Size of dataset for ", fName, " iter ", i, " :", length(histLoad)), stderr())

		if (length(histLoad) > 0) {
			xmin <- min(xmin, histLoad[[1]]$dataT$rows$bStart)
			xmax <- max(xmax, histLoad[[1]]$dataT$rows$bStart)

			gplot <- gplot + geom_line(data = histLoad[[1]]$dataT$rows,stat="identity", color=col[i+1]) #width=histLoad[[1]]$dataT$res, 
		}
	}	  

	pdf(file= paste0("Hist_", directory,".pdf"), width = 10, height = 10)
	gplot <- gplot + labs(x='occurrence of exeution value') +
		     scale_x_continuous(trans='log10') 
	#		 xlim(c(xmin,xmax)) 

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
	plotData(dirs[d])
}

# Find all directories with pattern.. UC1
dirs <- dir(".", pattern= "(^UC2|TEST[0-9]_2$)")

# Combine results of test batches
for (d in seq(along=dirs)){
	# Find all directories with pattern.. Test
	dirs2 <- dir(dirs[d], pattern= "^Test")

	# Combine results of test batches
	for (e in seq(along=dirs2)){
		plotData(dirs2[e])
	}
}

## back to the console
sink(type="output")

