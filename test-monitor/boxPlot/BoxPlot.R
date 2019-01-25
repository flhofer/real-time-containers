#Seven log boxplots with mean 
library(tidyr)
library(ggplot2)
library(reshape2)

myDataC<-read.csv("stats.csv", sep=";")[1:10000,1:12]

myData<-gather(myDataC, "mtype", value, "BM.Std", "BM.Xen", "BM.Prt", "T3.Aws", "T3.Xen", "T3.Xen.U", "T3.Prt", "T3.Prt.U", "C5.Aws","C5.Xen","C5.Prt", na.rm = FALSE, convert = FALSE)
myData<-myData[!(myData$value==0),]

oversh<-read.csv("stats.csv", sep=";")[10005:10005,1:12]
total<-read.csv("stats.csv", sep=";")[10001:10001,1:12]
oversh <- oversh / total * 100
oversh<-gather(oversh, "mtype", value, "BM.Std", "BM.Xen", "BM.Prt",  "T3.Aws", "T3.Xen", "T3.Xen.U", "T3.Prt", "T3.Prt.U", "C5.Aws","C5.Xen","C5.Prt", na.rm = FALSE, convert = FALSE)
oversh<-oversh[!(oversh$value==0),]

means<-read.csv("stats.csv", sep=";")[10003:10003,1:12]
means<-gather(means, "mtype", value, "BM.Std", "BM.Xen", "BM.Prt", "T3.Aws", "T3.Xen", "T3.Xen.U", "T3.Prt", "T3.Prt.U", "C5.Aws","C5.Xen","C5.Prt", na.rm = FALSE, convert = FALSE)

ggplot(myData, aes(x=mtype, y=Time)) + 
	scale_y_continuous(trans='log10') +
    geom_boxplot(fill="slateblue", alpha=0.2) +
    geom_point(data = oversh, aes(x = mtype, y = 11500, size=value), shape=17, , color="red", fill="red") +
    geom_point(data = means, aes(x = mtype, y = value), shape=20, size=5, color="blue", fill="blue") +
    labs(x="Machine type", y= expression(paste("Start time  ", mu, "sec")), size=">10 ms in\n% of set")+
    geom_hline(yintercept = 100, colour="#990000", linetype="dashed") +
    geom_hline(yintercept = 10000, colour="#990000", linetype="dashed") +
	theme(axis.text.x = element_text(angle = 90, hjust = 1))

