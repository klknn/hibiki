# Java Best Practices for Hibiki

Guidelines discovered during development and refactoring.

## Package-Private Visibility for Testability

When extracting functionality into helper classes within the same package, prefer **package-private** (no access modifier) over `private` for fields and methods that need cross-class access.

```java
// GOOD: Package-private for helper/renderer access
int keyHeight = 12;
JPanel gridPanel;
float getTickWidth() { ... }

// BAD: Forces reflection or public exposure
private int keyHeight = 12;
public int getKeyHeight() { return keyHeight; } // Unnecessarily broad
```

> [!TIP]
> Package-private is the narrowest visibility that allows access from test classes in the same package. Prefer it over `public` for test-only access.

## Delegation over Inheritance for Extraction

When extracting logic from large classes, use **composition/delegation** rather than inheritance:

```java
// GOOD: Delegate to handler
class TimelineMouseHandler {
    private final TimelineView view;
    void install() { /* wire up listeners */ }
}

// In TimelineView:
private final TimelineMouseHandler mouseHandler = new TimelineMouseHandler(this);
private void setupMouseListeners() { mouseHandler.install(); }
```

This keeps the original API surface unchanged while distributing code across files.

## Bazel Build Cache Pitfalls

When modifying method visibility (e.g., `private` → package-private), Bazel's Java compilation cache may not detect the change if only whitespace differs. After visibility changes:

```bash
# Force full recompilation if cached class files are stale
bazel clean && bazel build -c opt //:hibiki-gui-java
```

## Anonymous Inner Class Extraction Pattern

When extracting anonymous `MouseAdapter` / `MouseMotionAdapter` classes:

1. Create a named handler class that holds a reference to the parent
2. Move the anonymous class methods into private handler methods
3. Have the handler's `install()` method wire up the listeners
4. Replace the original method body with a delegation call

This preserves the same event wiring while enabling independent testing.

## Test Classes: Headless Guard

Swing tests that create UI components may fail in headless CI environments. Guard them:

```java
@Test
public void testSwingComponent() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    // ... test code ...
}
```

For pure logic tests (Note fields, enum values), no guard is needed.
