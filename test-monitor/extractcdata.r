# Set the working directory
setwd("./container/pretest/")
options("width"=200)
library(ggplot2)


loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	# cat (fName, "\n")

	if (!file.exists(fName)) {
		# break here if file does not exist
		dat <- data.frame ("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric(), "cDur"=numeric())
		return(dat)
	}

	if (file.info(fName)$size > 0) {
		# Transform into table, filter and add names
		dat = read.table(file= fName)
		#dat = read.table(text = tFile)
		dat <- data.frame ("RunT"=dat$V3,"Period"=dat$V4,"rStart"=dat$V7, "cDur"=dat$V9)
	}
	else {
		dat <- data.frame ("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric(), "cDur"=numeric())
	}
	
	return(dat)
}

#machines <- c("C5", "BM" , "T3", "T3U")
#tests <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10", "2-1", "2-2", "3-1", "3-2", "3-3",
# "4-1", "4-2", "4-3", "4-4", "4-5", "4-6", "4-7", "4-8", "4-9", "4-10")
machines <- c("BM") # "T3U", "C5", "BM"

#types <- c("test1", "test2", "test3", "test4", "test5", "test6", "test7", "test8")
types <- c("test1", "test2", "test3", "test4", "test5")
tests <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10", "2-1", "2-2", "3-1", "3-2", "3-3",
 "4-1", "4-2", "4-3", "4-4", "4-5", "4-6", "4-7", "4-8", "4-9", "4-10")

for (i in 1:length(machines)) {

	for (k in 1:length(tests)) {
		testPcount = 0;
		tplot <- data.frame()

		for (j in 1:length(types)) {


			# Label and directory pattern
			cat(machines[i], "-", types[j], "-", tests[k], "\n")
			
			# Find all directories with pattern.. 
			dir = paste0(machines[i], "/", types[j], "/", tests[k])

			maxAll = 0
			pcountAll = 0

			r<- data.frame (matrix(ncol=13,nrow=0))
			plot <- data.frame()
			names(r) <- c("Min", "Mdn", "Avg", "AvgDif","AvgDev", "Max", "pMin", "pMdn", "pAvg", "pavgDev", "pMax", "pOVPeak", "pcount")
			nr = 0
			files <- list.files(path=dir, pattern="*.log", full.names=TRUE, recursive=FALSE)
			for (x in files) {

				nr = nr +1
				dat <- loadData(x)
				datp <- dat[!(dat$RunT > dat$cDur*1.5),]

				minMin = min(datp$RunT)
				avgMea = mean(datp$RunT)
				avgMed = median(datp$RunT)
				avgDev = sqrt(var(datp$RunT))
				avgDif = avgMed-avgMea;
				maxMax = max(datp$RunT)
				pminMin = min(datp$Period)
				pavgMea = mean(datp$Period)
				pavgMdn = median(datp$Period)
				pavgDev = sqrt(var(datp$Period))
				pmaxMax = max(datp$Period)
				pmaxMaxp = max(dat$Period)
				pcount = sum ( dat$Period > pavgMea*1.5)
				maxAll = max(maxMax, maxAll)
				pcountAll = pcountAll + pcount

				r[nrow(r)+1,] <-data.frame (minMin, avgMed, avgMea, avgDif, avgDev, maxMax, pminMin, pavgMdn, pavgMea, pavgDev, pmaxMax, pmaxMaxp, pcount)
				plot <- rbind(plot, dat)
			}
			plot$type <- types[j]
			plot$oversh <- pcountAll
			tplot<-rbind(tplot,plot)
			print(r)
			cat ("Peak - Peak count ", maxAll, pcountAll, "\n")
			testPcount = testPcount + pcountAll
  		}

  		head(tplot)
  		pdf(file= paste0(machines[i],"_" , tests[k],".pdf"), width = 10, height = 10)
 
	    # geom_point(data = means, aes(x = mtype, y = value), shape=20, size=5, color="blue", fill="blue") +
	    # geom_hline(yintercept = 100, colour="#990000", linetype="dashed") +
	    # geom_hline(yintercept = 10000, colour="#990000", linetype="dashed") +
		# theme(axis.text.x = element_text(angle = 90, hjust = 1))


		tplot <- tplot[!((tplot$RunT > tplot$cDur*1.5)),] # drop exceeding points
  		hist <- ggplot(tplot, aes(x=type, y=RunT), col = rainbow(7)) + 
			scale_y_continuous(trans='log10') +
  	# 		ylim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
		 	geom_boxplot(fill="slateblue", alpha=0.2) +
  			# xlim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
		    geom_point(data = tplot, aes(x = type, y = tplot$cDur*1.5, size=oversh), shape=17, , color="red", fill="red") +
		    labs(x="Test type", y= expression(paste("Run-time  (values are in ", mu, "s)")), size=">10 ms in\n% of set")+
			scale_fill_viridis_d() 
  		print (hist)
  		dev.off()

        # head(tplot)
        # pdf(file= paste0(machines[i],"_" , tests[k],".pdf"), width = 10, height = 10)
        # hist <- ggplot(tplot, aes(x=RunT, fill = types), col = rainbow(7)) + 
        #         geom_density(alpha = 0.1) +
        #         xlim (c(tplot$cDur[1]*0.95,tplot$cDur[1]*1.05)) + 
        #         ylab("Count") +
        #         scale_fill_viridis_d()
        # print (hist)
        # dev.off()

		cat ("Test set overruns ", testPcount , "\n")
		cat ("----------------------------------------\n")
	}
}
