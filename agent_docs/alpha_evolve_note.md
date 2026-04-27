Goal:
Use iterative code modification + evaluator loop.

Key ideas:
- propose one heuristic scheduling algorithm for each iteration
- run evaluator automatically
- compare the performance, power and area with baseline and previous best
- keep only improved candidates
- log all failed attempts

work flow:
- strict evaluation after each change
- rollback on failure
- keep best-so-far