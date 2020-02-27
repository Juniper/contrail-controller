package bench

import (
	"math"
	"sync"
	"time"

	"github.com/sirupsen/logrus"
)

// an utility tool for benchmark.

type stat struct {
	duration time.Duration
	success  bool
}

type worker struct {
	id        int
	startTime time.Time
	endTime   time.Time
	stats     []*stat
}

type task func(workerID, loopCount int) error
type workers []*worker

func (w *worker) run(t task, loopCount int) {
	for i := 0; i < loopCount; i++ {
		startTime := time.Now()
		err := t(w.id, i)
		if err != nil {
			logrus.Debugf("[WorkerID: %d, Loop: %d] %s", w.id, i, err)
		}
		w.stats = append(w.stats, &stat{
			duration: time.Now().Sub(startTime),
			success:  err == nil,
		})
	}
	w.endTime = time.Now()
}

func (w *worker) duration() time.Duration {
	return w.endTime.Sub(w.startTime)
}

func (ws workers) stats() {
	var successCount, errorCount int64
	var totalTime time.Duration
	for _, w := range ws {
		for _, stat := range w.stats {
			if stat.success {
				successCount++
			} else {
				errorCount++
			}
			totalTime = totalTime + stat.duration
		}
	}
	totalCount := successCount + errorCount
	logrus.Info("Success Count: ", successCount)
	logrus.Info("Error Count: ", errorCount)
	logrus.Info("Success rate: ", float64(successCount)/float64(totalCount)*100)
	logrus.Info("Total time: ", totalTime)
	logrus.Info("Request per sec: ", math.Floor(float64(totalCount)/totalTime.Seconds()))
}

func newWorker(id int) *worker {
	return &worker{
		id:        id,
		startTime: time.Now(),
		stats:     []*stat{},
	}
}

//Benchmark runs benchmark test.
func Benchmark(numWorker int, loopCount int, t task) {
	wg := sync.WaitGroup{}
	ws := workers{}
	for i := 0; i < numWorker; i++ {
		wg.Add(1)
		w := newWorker(i)
		ws = append(ws, w)
		w.run(t, loopCount)
		wg.Done()
	}
	wg.Wait()
	ws.stats()
}
