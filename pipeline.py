import requests
import json
import re
import time
import subprocess

# Replace this with your Cloudflare Tunnel URL from Colab
OLLAMA_API = "https://rough-iv-um-architecture.trycloudflare.com/api"
MODEL_NAME = "qwen2.5-coder:7b"
CONTAINER_NAME = "genmc"

# File path to the C file
MY_C_PATH = r"C:\Users\alexd\Uni\Concurrent-Datastructure-Model-Checker\tests\correct\data-structures\treiber-stack\my_stack.c"

OUTPUT_TEMPLATE = """unsigned int pop(mystack_t *s)
{
	pointer oldTop, newTop, next;
	node_t *node;
	bool success;
	int val;
	while (true) {
        ???
	}
	val = node->value;
	/* Reclaim the used slot */
	reclaim(get_ptr(oldTop));
	return val;
}
"""

original_system_prompt = """
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>

#include "my_stack.h"

#ifdef MAKE_ALL_SC
# define release memory_order_seq_cst
# define acquire memory_order_seq_cst
# define relaxed memory_order_seq_cst
#else
# define release memory_order_release
# define acquire memory_order_acquire
# define relaxed memory_order_relaxed
#endif

#ifndef MAX_THREADS
# define MAX_THREADS 32
#endif

#define MAX_FREELIST 4 /* Each thread can own up to MAX_FREELIST free nodes */
#define INITIAL_FREE 2 /* Each thread starts with INITIAL_FREE free nodes */

#define POISON_IDX 0x666

static unsigned int free_lists[MAX_THREADS][MAX_FREELIST];

/* Search this thread's free list for a "new" node */
static unsigned int new_node()
{
	int i;
	int t = get_thread_num();
	for (i = 0; i < MAX_FREELIST; i++) {
		//unsigned int node = load_32(&free_lists[t][i]);
		unsigned int node = free_lists[t][i];
		if (node) {
			//store_32(&free_lists[t][i], 0);
			free_lists[t][i] = 0;
			return node;
		}
	}
	/* free_list is empty? */
	assert(0);
	return 0;
}

/* Place this node index back on this thread's free list */
static void reclaim(unsigned int node)
{
	int i;
	int t = get_thread_num();

	/* Don't reclaim NULL node */
	assert(node);

	for (i = 0; i < MAX_FREELIST; i++) {
		/* Should never race with our own thread here */
		//unsigned int idx = load_32(&free_lists[t][i]);
		unsigned int idx = free_lists[t][i];

		/* Found empty spot in free list */
		if (idx == 0) {
			//store_32(&free_lists[t][i], node);
			free_lists[t][i] = node;
			return;
		}
	}
	/* free list is full? */
	assert(0);
}

void init_stack(mystack_t *s, int num_threads)
{
	int i, j;

	/* Initialize each thread's free list with INITIAL_FREE pointers */
	/* The actual nodes are initialized with poison indexes */
//	free_lists = malloc(num_threads * sizeof(*free_lists)); /* Statically initialized */
	for (i = 0; i < num_threads; i++) {
		for (j = 0; j < INITIAL_FREE; j++) {
			free_lists[i][j] = 1 + i * MAX_FREELIST + j;
			atomic_init(&s->nodes[free_lists[i][j]].next, MAKE_POINTER(POISON_IDX, 0));
		}
	}

	/* initialize stack */
	atomic_init(&s->top, MAKE_POINTER(0, 0));
}

void push(mystack_t *s, unsigned int val) {
	unsigned int nodeIdx = new_node();
	node_t *node = &s->nodes[nodeIdx];
	node->value = val;
	pointer oldTop, newTop;
	bool success;
	while (true) {
		// acquire
		oldTop = atomic_load_explicit(&s->top, acquire);
		newTop = MAKE_POINTER(nodeIdx, get_count(oldTop) + 1);
		// relaxed
		atomic_store_explicit(&node->next, oldTop, relaxed);

		// release & relaxed
		success = atomic_compare_exchange_strong_explicit(&s->top, &oldTop,
			newTop, release, relaxed);
		if (success)
			break;
	}
}
"""

original_user_prompt = """<｜fim▁begin｜>unsigned int pop(mystack_t *s)
{
	pointer oldTop, newTop, next;
	node_t *node;
	bool success;
	int val;
	while (true) {
	    <｜fim▁hole｜>
    }
	val = node->value;
	/* Reclaim the used slot */
	reclaim(get_ptr(oldTop));
	return val;
}<｜fim▁end｜>

// High-Level Instructions:
// - Implement a thread-safe pop operation for a lock-free stack.
// - Handle the edge case where the stack is empty and return 0.
// - Ensure atomic operations correctly update both the pointer and the counter to maintain consistency.

"""
# Read the original file content containing the placeholder "???" once.
with open(MY_C_PATH, "r", encoding="utf-8") as f:
    BASE_FILE_CONTENT = f.read()
    

conversation = [
    {
        "role": "system",
        "content": (
            "You are a strict C programming assistant. Your only task is to fill in the missing C code for a user provided function. Here is the context code:\n"
            + original_system_prompt +
			"\n- You MUST return **only C code**.\n"
            "\n- You MUST respect the following output template: \n" + OUTPUT_TEMPLATE +
            "\n- Do NOT include any explanations, comments, or additional text.\n"
            )
    },
    {
        "role": "user",
        "content": original_user_prompt
    }
]

def is_model_loaded():
    """Check if the model is available in Ollama."""
    try:
        response = requests.get(
            f"{OLLAMA_API}/tags",
            allow_redirects=False,
            timeout=5
        )
        print(response.status_code, response.headers.get("Location"))
        response.raise_for_status()
        models = response.json().get("models", [])
        return any(model["name"] == MODEL_NAME for model in models)
    except Exception as e:
        print(f"⚠️ Failed to check model availability: {e}")
        return False

def load_model():
    """Trigger loading of the model if not available."""
    if not is_model_loaded():
        print(f"⏳ Loading model: {MODEL_NAME}...")
        try:
            response = requests.post(f"{OLLAMA_API}/pull", json={"name": MODEL_NAME})
            response.raise_for_status()
            print(f"✅ Model {MODEL_NAME} loaded successfully!")
        except Exception as e:
            print(f"❌ Failed to load model: {e}")

def fill_in_function_code(filled_code):
    """
    Replace the ??? placeholder with the generated function code.
    Always uses the original file content (stored in BASE_FILE_CONTENT)
    to ensure the placeholder is present.
    """
    updated_content = BASE_FILE_CONTENT.replace("???", filled_code)
    with open(MY_C_PATH, "w", encoding="utf-8") as f:
        f.write(updated_content)
    print("✅ Function code inserted into my_stack.c.")

def call_ollama(prompt):
    """Send a structured chat request to the Ollama API and return the response."""
    payload = {
        "model": MODEL_NAME,
        "messages": [
            {"role": "system", "content": (
                "You are a strict C programming assistant. Your only task is to fill in the missing C code for a provided function.\n"
                "Rules:\n"
                "- The output MUST be exactly in the following format:\n"
                + OUTPUT_FORMAT +
                "\n- You MUST return **only C code** enclosed within [C] and [\\C]. Do not generate code in any other language.\n"
                "- Do NOT include any explanations, comments, or additional text.\n"
                "- Do NOT modify any pre-existing code; only fill in the missing section marked by ???.\n"
                "- Do NOT create any new functions; only fill in the missing section marked by ???.\n"
                "- The entire response must be exactly as specified, with no extra characters or whitespace outside the [C] [\\C] markers."
            )},
            {"role": "user", "content": prompt}
        ]
    }

    try:
        response = requests.post(f"{OLLAMA_API}/chat", json=payload, stream=True)
        response.raise_for_status()
        
        # Collect streamed responses
        generated_text = ""
        for line in response.iter_lines():
            if line:
                try:
                    data = json.loads(line.decode("utf-8"))
                    if "message" in data:
                        generated_text += data["message"]["content"]
                        print(data["message"]["content"], end="", flush=True)
                except json.JSONDecodeError:
                    print("\n❌ JSON decoding failed for:", line)

        if not generated_text:
            print("\n❌ No valid response received.")
            return None

        return generated_text

    except Exception as e:
        return f"⚠️ Connection failed: {e}"
    
def call_ollama_generate(prompt, request_timeout=30):
    """Send a single-prompt generation request to the Ollama API."""
    payload = {
        "model": MODEL_NAME,
        "prompt": prompt,
        "options": {"num_ctx": 16384},
        "raw": True
    }
    try:
        response = requests.post(f"{OLLAMA_API}/generate", json=payload, stream=True, timeout=request_timeout)
        response.raise_for_status()
        generated = ""
        for line in response.iter_lines():
            if line:
                try:
                    data = json.loads(line.decode("utf-8"))
                    if "token" in data and "text" in data["token"]:
                        text = data["token"]["text"]
                        generated += text
                        print(text, end="", flush=True)
                except json.JSONDecodeError:
                    print("❌ JSON decoding failed for line in generate:", line)
        if not generated:
            print("❌ No valid generation received.")
            return None
        return generated
    except Exception as e:
        print(f"⚠️ Connection failed for generate: {e}")
        return None

def call_ollama_with_history(conv_history, request_timeout=30):
    """Send a structured chat request to the Ollama API using conversation history."""
    payload = {
        "model": MODEL_NAME,
        "messages": conv_history, 
        "options": {"num_ctx": 16384},
        "raw": True
    }
    try:
        response = requests.post(f"{OLLAMA_API}/chat", json=payload, stream=True, timeout=request_timeout)
        response.raise_for_status()
        generated_text = ""
        for line in response.iter_lines():
            if line:
                try:
                    data = json.loads(line.decode("utf-8"))
                    if "message" in data:
                        generated_text += data["message"]["content"]
                        print(data["message"]["content"], end="", flush=True)
                except json.JSONDecodeError:
                    print("\n❌ JSON decoding failed for:", line)
                except Exception as e: 
                    print(f"⚠️ Ollama request failed or timed out: {e}")
        if not generated_text:
            print("\n❌ No valid response received.")
            return None
        return generated_text
    except Exception as e:
        return f"⚠️ Connection failed: {e}"

def candidate_respects_template(candidate):
    """Check that the candidate output includes all fixed parts from the output template."""
    required_parts = [line.strip() for line in OUTPUT_TEMPLATE.splitlines() if '???' not in line.strip()]
    for part in required_parts:
        if part not in candidate:
            print(f"Missing expected part: {part}")
            return (False, part)
    return (True,)

def run_tests_in_container():
    """
    Runs the test binary in the Docker container.
    Waits up to 2 seconds before timing out.
    Returns:
      - (True, output) if tests pass
      - (False, error_message) if tests fail, abort, or timeout
    """
    run_cmd = ["docker", "exec", CONTAINER_NAME, "/tmp/test_stack"]
    try:
        result = subprocess.run(
            run_cmd,
            check=True,
            capture_output=True,
            text=True,
            timeout=2
        )
        print("Test output:\n", result.stdout)
        return True, result.stdout

    except subprocess.TimeoutExpired:
        error_message = "Test run timed out after 2 seconds."
        print(error_message)
        return False, error_message

    except subprocess.CalledProcessError as e:
        # The test binary exited non-zero (e.g. abort/assert failure)
        stdout = e.stdout or ""
        stderr = e.stderr or ""
        error_message = (
            f"Test binary aborted with exit code {e.returncode}\n"
            f"=== STDOUT ===\n{stdout}\n"
            f"=== STDERR ===\n{stderr}"
        )
        print(error_message)
        return False, error_message
    
def run_compile_in_container():
    """
    Compiles and runs the tests in the Docker container.
    Waits up to 2 seconds for the test run before canceling.
    Returns (True, output) if tests pass; (False, error_message) if tests fail or time out.
    """
    compile_cmd = [
        "docker", "exec", CONTAINER_NAME,
        "gcc", "-o", "/tmp/test_stack",
        "/root/genmc/tests/correct/data-structures/treiber-stack/test_stack.c",
        "-lpthread", "-std=c11"
    ]
    try:
        result = subprocess.run(compile_cmd, check=True, capture_output=True, text=True)
        output = (result.stdout or "") + (result.stderr or "")
        return True, output
    except subprocess.CalledProcessError as e:
        error_message = (e.stderr or "") + "\n" + (e.stdout or "")
        return False, error_message

import subprocess
import time
import signal

def run_genmc_in_container(container_name=CONTAINER_NAME, timeout_sec=30):
    # This assumes you have installed `ps` in the container.
    exec_cmd = [
        "docker", "exec", container_name,
        "/root/genmc/src/genmc",
        "-model-file", "/root/genmc/tests/configs/stack/c-stack-treiber.txt",
        "/root/genmc/tests/correct/data-structures/treiber-stack/variants/main0.c"
    ]

    # Run the docker exec as a subprocess
    process = subprocess.Popen(
        exec_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        stdout, stderr = process.communicate(timeout=timeout_sec)
        return stdout
    except subprocess.TimeoutExpired:
        print("Timeout reached. Cleaning up...")

        # Step 1: Find the PID inside the container
        find_pid_cmd = [
            "docker", "exec", container_name, "pidof", "genmc"
        ]
        try:
            pid_output = subprocess.check_output(find_pid_cmd, text=True).strip()
            pids = pid_output.split()
            for pid in pids:
                kill_cmd = ["docker", "exec", container_name, "sh", "-c", f"kill -9 {pid}"]
                subprocess.run(kill_cmd)
                print(f"Killed genmc process with PID {pid} inside container.")
        except subprocess.CalledProcessError:
            print("Warning: Unable to find or kill genmc PID inside container.")

        process.kill()
        process.wait()
        return "Timed out."


if __name__ == "__main__":
    load_model()
    max_batches = 10
    max_attempts_per_batch = 10

    # Keep the initial conversation template
    initial_conversation = conversation[:]  # Copy of the initial conversation

    # Correct solution for BLEU evaluation (string of code)
    correct_solution = ''''''

    # Statistics
    successful_batches = 0
    total_attempts_for_success = 0
    bleu_scores = []
    durations = []

    # We'll only perform the compile check (first experiment)
    for batch in range(1, max_batches + 1):
        print(f"\n=== Starting Batch {batch} ===")
        attempt = 0
        compiled = False
        first_compiled = False
        start_time = time.time()
        ok = False
        # Reset to initial conversation at the start of each batch
        conv = initial_conversation[:]

        while attempt < max_attempts_per_batch:
            attempt += 1
            print(conv)
            print(f"\n--- Attempt {attempt} ---")

            # Call the model with the current conversation
            response = call_ollama_with_history(conv, request_timeout=30)
            if not response:
                print("No valid response (or request timed out). Retrying.")
                continue
            if not first_compiled:
                # Extract candidate code
                match = re.search(r"```c\s*(.*?)\s*```", response, re.DOTALL)
                if not match:
                    feedback = "No valid ```c ... ``` block found."
                    print(feedback)
                    conv = initial_conversation[:-1] + [
                        {"role": "assistant", "content": response or ""},
                        {"role": "user", "content": feedback}
                    ]
                    continue

                conv = initial_conversation[:-1]
                candidate_code = match.group(1).strip()
                print(candidate_code)
                conv.append({"role": "assistant", "content": f"```c\n{candidate_code}\n```"})

                # Pre-compile checks
                if "???" in candidate_code:
                    feedback = "Candidate code still has ???."
                    print(feedback)
                    conv.append({"role": "user", "content": feedback})
                    continue

                if not candidate_respects_template(candidate_code)[0]:
                    feedback = "Candidate code fails template check. Missing expected part: " + candidate_respects_template(candidate_code)[1]
                    print(feedback)
                    conv.append({"role": "user", "content": feedback})
                    continue

                # Write candidate to file
                fill_in_function_code(candidate_code)

                # COMPILE CHECK ONLY: run compile (without unit tests)
                compiled, compile_output = run_compile_in_container()
                if not compiled:
                    feedback = f"Compilation failed with errors:\n{compile_output}"
                    print(feedback)
                    conv.append({"role": "user", "content": feedback})
                    continue
                else:
                    first_compiled = True
            else:
                # Extract candidate code
                match = re.search(r"```c\s*(.*?)\s*```", response, re.DOTALL)
                if not match:
                    continue

                candidate_code = match.group(1).strip()
                print(candidate_code)

                # Pre-compile checks
                if "???" in candidate_code:
                    continue

                if not candidate_respects_template(candidate_code)[0]:
                    continue

                # Write candidate to file
                fill_in_function_code(candidate_code)

                compiled, compile_output = run_compile_in_container()
                if not compiled:
                    continue
                else:
                    conv = initial_conversation[:-1]
                    conv.append({"role": "assistant", "content": f"```c\n{candidate_code}\n```"})
            # If compilation succeeds, record success and stats, then move to next batch
            print("✅ Compilation succeeded.")

             # Run unit tests
            tests_res = run_tests_in_container()
            tests_ok, test_out = tests_res
            if not tests_ok:
                fb = f"The implementation you provided fails the following unit tests:\n{test_out}. Update the implementation accordingly."
                print(fb)
                ok = True
                conv.append({"role": "user", "content": fb})
                continue
            print("✅ Unit tests passed.")

            # Run GenMC
            genmc_out = run_genmc_in_container()
            if "Timed out" in genmc_out:
                attempt = 11
                continue
            if "7" not in genmc_out:
                fb = f"GenMC errors:\n{genmc_out}"
                print(fb)
                ok = True
                conv.append({"role": "user", "content": fb})
                continue
            print("✅ GenMC passed.")

            successful_batches += 1
            total_attempts_for_success += attempt

            # Compute BLEU for this candidate against the correct solution
            from sacrebleu import sentence_bleu
            score = sentence_bleu(
                candidate_code,
                [correct_solution],
                smooth_method='exp'
            ).score
            bleu_scores.append(score)
            print(f"🔢 BLEU score for batch {batch}: {score:.2f}")
            
            compiled = True
            durations.append(time.time() - start_time)
            break  # End attempts for this batch
        # Continue to next batch regardless of compiled or not

    # After all batches, compute aggregate statistics
    print(f"\n🏁 Experiment complete over {max_batches} batches.")
    print(f"Successful batches: {successful_batches}/{max_batches}")
    if successful_batches > 0:
        avg_bleu = sum(bleu_scores) / len(bleu_scores)
        avg_attempts = total_attempts_for_success / successful_batches
        print(f"Average BLEU over successful batches: {avg_bleu:.2f}")
        print(f"Average attempts to compile: {avg_attempts:.1f}")
    else:
        print("No successful compilations to report BLEU or attempts.")

    if durations:
        avg_time = sum(durations) / len(durations)
        print(f"Avg time per attempt: {avg_time:.2f} seconds")
    fill_in_function_code("???")