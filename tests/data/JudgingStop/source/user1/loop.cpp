// Never terminates, so a judging round can only finish early by being stopped.
// The time limit in JudgingStop.cdf is 30000 ms, which is far longer than the
// budget the judging-stop test allows, so the two behaviours cannot be confused.
//
// `volatile` keeps the loop from being optimised away into a no-op.
int main() {
	volatile unsigned long long counter = 0;

	while (true)
		counter = counter + 1;

	return 0;
}
