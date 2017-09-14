import sys
import csv
import json
import matplotlib

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

def plotGraph(report, keys):
    with open(report, 'rb') as csvfile:
        csvReader = csv.reader(csvfile, delimiter='|', quotechar='$')
        dictReader = csv.DictReader(csvfile, delimiter='|', quotechar='$')

        byN = dict()
        byDelay = dict()

        # populate byN
        for row in dictReader:
            tag = json.loads(row["tag"])
            numPeers = tag["numPeers"]
            delay = tag["delay"]

            if not numPeers in byN:
                byN[numPeers] = dict()
            byN[numPeers][delay] = row
            if not delay in byDelay:
                byDelay[delay] = dict()
            byDelay[delay][numPeers] = row

    ## tx.csv
    for (numPeers, delayRows) in sorted(byN.iteritems()):
        delays = list()
        values = dict()
        for key in keys:
            values[key] = list()

        # collect measured values into lists by delay for a given numPeers
        for (delay, row) in sorted(delayRows.iteritems()):
            delays.append(delay)
            for key in keys:
                values[key].append(row[key])

        # plot one line on each figure corresponding to run of sim with fixed
        # numPeers and varying delays for a given measure

        for key in keys:
            plt.figure(key)
            plt.plot(delays, values[key], alpha=0.2, label=str(numPeers))

    pdf = PdfPages(report + ".pdf")

    for key in keys:
        fig = plt.figure(key)
        plt.title(report + "\n" + key)
        plt.xlabel("delays")
        plt.ylabel(key)
        #plt.legend(loc='best')

        pdf.savefig(fig)
    pdf.close()

txStats = (
    "txNumSubmitted",
    "txNumAccepted",
    "txNumValidated",
    "txNumOrphaned",
    "txUnvalidated",
    "txRateSumbitted",
    "txRateAccepted",
    "txRateValidated",
    "txLatencySubmitToAccept10Pctl",
    "txLatencySubmitToAccept50Pctl",
    "txLatencySubmitToAccept90Pctl",
    "txLatencySubmitToValidate10Pctl",
    "txLatencySubmitToValidate50Pctl",
    "txLatencySubmitToValidate90Pctl",
    )

ledgerStats = (
    "ledgerNumAccepted",
    "ledgerNumFullyValidated",
    "ledgerRateAccepted",
    "ledgerRateFullyValidated",
    "ledgerLatencyAcceptToAccept10Pctl",
    "ledgerLatencyAcceptToAccept50Pctl",
    "ledgerLatencyAcceptToAccept90Pctl",
    "ledgerLatencyFullyValidToFullyValid10Pctl",
    "ledgerLatencyFullyValidToFullyValid50Pctl",
    "ledgerLatencyFullyValidToFullyValid90Pctl",
    )

for report in sys.argv[1:]:
    if "tx" in report:
        keys = txStats
        print("Generating tx graphs from '" + report + "'")
    elif "ledger" in report:
        keys = ledgerStats
        print("Generating ledger graphs from '" + report + "'")
    plotGraph(report, keys)


