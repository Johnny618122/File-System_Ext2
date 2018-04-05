The self-tester is a tool that makes it easier to compare your implementation
to the solution, helping you to identify bugs and to know what is considered 
correct behaviour.

How to run:
1. Copy this directory into your MAIN directory (where you have your ext2_cp, etc.).   
2. Create a directory called images in your MAIN directory and place all images from the assignment handout there. The self-tester uses some of those images in addition to a few of its own.   
3. cd to your MAIN directory
4. (From that MAIN directory) Run self-tester/autorun.sh 

results: contains the dumps for each test case.
runs: populated with the images from running each test case.
solution-results: contains the dumps from running the solution on each test case.

Compare what's in solution-results with what's in results. You can use something 
like diff to help spot differences between the two.

Notes:
- The particular allocation order for the blocks that comprise a file and the
    indirect blocks of the file is not important. The solution does it in an odd
	order, but you don't need to be concerned if you choose not to do the same.
	This (the allocation order of the blocks within a file) is the only
	exception, otherwise the order that you allocate blocks should match.
- The inode reference mapping display ignores file size. It just scans the
    reference arrays up to their capacity.
	

Please report any problems or disagreements you encounter with using this tool
or with the solution results on piazza.
