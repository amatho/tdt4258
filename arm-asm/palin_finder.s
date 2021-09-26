.global _start

.section .text

_start:
	// Use the input as an argument for the subroutine
	ldr r0, =input
	bl input_len

	// Set up arguments for the subroutine
	mov r1, r0
	ldr r0, =input
	bl is_palindrome

	// Call the appropriate subroutine based on whether the input was a palindrome or not
	cmp r0, #1
	bleq palindrome_found
	blne palindrome_not_found

	// Branch to the exit loop
	b exit

/*
 * input_len - Get the length of the given string.
 *
 * Arguments:
 * R0 - Address of a nul-terminated string
 *
 * Returns:
 * R0 - Length of the string
 */
input_len:
	// Branch if the string is empty
	ldrb r1, [r0] // A character is a byte, so we need to use Load Byte
	cmp r1, #0
	beq input_len_empty

	mov r1, #0 // Use R1 as the string length

input_len_loop:
	// Increment the length and check if the next character is nul.
	// If it's nul, return, else run the loop again
	add r1, r1, #1
	ldrb r2, [r0, r1] // Load the next character into R2
	cmp r2, #0
	bne input_len_loop
	mov r0, r1 // Move length to return register
	bx lr

input_len_empty:
	mov r0, #0 // Set length to 0
	bx lr

/*
 * is_palindrome - Check if a string is a palindrome.
 *
 * Arguments:
 * R0 - Address of a nul-terminated
 * R1 - Length of the string given in R0
 *
 * Returns:
 * R0 - 1 if the string was a palindrome, 0 otherwise
 */
is_palindrome:
	subs r1, r1, #1 // Use R1 as the index of the last character
	bls is_palindrome_true // Branch if the length of the string was less than or equal to 1
	mov r2, #0 // Use R0 as the index of the first character
	b is_palindrome_loop_inner

is_palindrome_loop:
	cmp r2, r1 // Check if the first and last index are overlapping
	bcs is_palindrome_true // In that case, branch to the true condition

is_palindrome_loop_inner:
	ldrb r3, [r0, r2] // Load the first character
	// If the character is a space (ASCII 0x20), increment the index and loop again
	cmp r3, #0x20
	addeq r2, r2, #1
	beq is_palindrome_loop

	ldrb ip, [r0, r1] // Load the last character
	// If the character is a space (ASCII 0x20), decrement the index and loop again
	cmp ip, #0x20
	subeq r1, r1, #1
	beq is_palindrome_loop

	// If the first or last character is lower case (ASCII > 0x60), convert to upper case
	// by subtracting 0x20
	cmp r3, #0x60
	subhi r3, r3, #0x20
	cmp ip, #0x60
	subhi ip, ip, #0x20

	// Compare the first and last characters. If they are not equal, the string is not a
	// palindrome. Otherwise, increment and decrement the first and last index respectively
	// and loop again
	cmp r3, ip
	bne is_palindrome_false
	add r2, r2, #1
	sub r1, r1, #1
	b is_palindrome_loop

is_palindrome_true:
	mov r0, #1 // Return true (1) in R0
	bx lr

is_palindrome_false:
	mov r0, #0 // Return false (0) in R0
	bx lr

/*
 * palindrome_found - Switch on LEDs and print a message, indicating that a palindrome
 * has been found.
 */
palindrome_found:
	// Push the link register onto the stack, since this subroutine calls another
	// subroutine
	push {lr}

	ldr r0, =0xFF200000 // Address of the LED data register
	mov r1, #0x1F // Load 0b0001_1111 into R1
	str r1, [r0] // Write to the LED data register

	// Print the message
	ldr r0, =found
	bl put_string

	// Pop the link register directly into the program counter from the stack, effectively doing the
	// same as a Branch and Exchange (bx lr)
	pop {pc}

/*
 * palindrome_not_found - Switch on LEDs and print a message, indicating that a palindrome
 * has not been found.
 */
palindrome_not_found:
	// Push the link register onto the stack, since this subroutine calls another
	// subroutine
	push {lr}

	ldr r0, =0xFF200000 // Address of the LED data register
	mov r1, #0x3E0 // Load 0b0011_1110_0000 into R1
	str r1, [r0] // Write to the LED data register 

	// Print the message
	ldr r0, =not_found
	bl put_string

	// Pop the link register directly into the program counter from the stack, effectively doing the
	// same as a Branch and Exchange (bx lr)
	pop {pc}

/*
 * put_string - Print the given string by writing to the JTAG UART.
 *
 * Arguments:
 * R0 - address of a nul-terminated string
 */
put_string:
	ldrb r1, [r0] // Load the next character
	cmp r1, #0 // Check if the character is a nul character
	beq put_string_cont

	ldr r2, =0xFF201000 // Address of the JTAG UART data register
	str r1, [r2] // Write the character to the data register
	add r0, r0, #1 // Increment the address of the string
	b put_string

put_string_cont:
	bx lr

exit:
	b exit

.section .data
	// This is the input you are supposed to check for a palindrome
	// You can modify the string during development, however you
	// are not allowed to change the label 'input'!
	input: .asciz "level"
	// input: .asciz "8448"
    // input: .asciz "KayAk"
    // input: .asciz "step on no pets"
    // input: .asciz "Never odd or even"

	found: .asciz "Palindrome detected\n"
	not_found: .asciz "Not a palindrome\n"

.end
