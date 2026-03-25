# Project: arcana-embedded-stm32

## Session Checkpoint Protocol (Anti-Crash)

Claude MUST follow this protocol to protect against CLI crashes:

### Rule 1: Checkpoint After Every Step
After completing any meaningful step (design decision, code written, file edited, build tested), IMMEDIATELY update the checkpoint file:
```
~/.claude/projects/-Users-jrjohn-Documents-projects-arcana-embedded-stm32/memory/session_checkpoint.md
```

### Rule 2: Checkpoint Format
The checkpoint file must contain:
- **Task**: what we're working on (1 line)
- **Completed**: numbered list of steps done this session
- **Current**: what was just completed or in-progress
- **Next**: what comes next
- **Key Decisions**: any design decisions or user preferences captured
- **Updated**: ISO timestamp

### Rule 3: Session Start
At the start of every session, check if `session_checkpoint.md` exists and has recent content. If the user's question relates to the checkpoint topic, proactively mention: "I see we were working on [X]. Want to continue?"

### Rule 4: Memory Promotion
When a checkpoint item represents a **permanent decision** (architecture, user preference, design choice), also save it as a proper memory file (project/feedback/user type) so it persists even after the checkpoint is cleared.

### Rule 5: Checkpoint Cleanup
When a task is fully complete (deployed, merged, or user confirms done), clear the checkpoint file content but keep the file.
