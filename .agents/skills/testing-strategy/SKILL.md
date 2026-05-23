---
name: testing-strategy
description: Testing guidelines and strategies for the Hibiki DAW workspace, including writing failing tests first to reproduce bugs, C++ unit/integration testing with Google Test, Java UI/integration testing with JUnit, and running tests via Bazel.
version: 1.0.0
tags:
  - testing
  - gtest
  - junit
  - bazel
  - development-standards
---

# Testing Strategy and Guidelines

Use this skill when you need to write, run, or debug tests in this codebase, or when implementing bug fixes or new features that require test coverage.

## 1. Test-Driven Bug Fixing ("Test First")
Always write a failing regression test *before* applying a bug fix.
1. Identify the bug report or symptom description.
2. Revert any local changes related to the fix so you are on the buggy version of the codebase.
3. Write a test case (C++ or Java) that triggers the bug and asserts the correct (expected) behavior.
4. Run the test and verify that it compiles and fails on the buggy codebase.
5. Apply your fix.
6. Verify that the test now passes successfully.
7. Run all repository tests (`bazel test //... -c opt`) to ensure no regressions are introduced.

## 2. C++ Testing (Engine / Backend)
- **Framework**: Google Test (gtest) and Google Mock (gmock).
- **Location**: Test files are placed alongside code under `engine/`, e.g. `engine/core/project_test.cpp`, `engine/instruments/builtin_sampler_test.cpp`.
- **Anatomy of a C++ Test**:
  ```cpp
  #include <gtest/gtest.h>
  #include "engine/core/project.hpp"

  TEST_F(ProjectTest, MyNewFeatureTest) {
    hibiki::ProjectState state;
    // Set up test inputs
    ...
    // Perform operations
    ...
    // Assert results
    EXPECT_EQ(result, expected);
  }
  ```
- **Running C++ Tests**:
  - Run all tests in a package:
    `bazel test //engine/core:all -c opt --test_output=all`
  - Run a specific test with a filter:
    `bazel test //engine/core:project_test --test_filter="*MyNewFeatureTest*" -c opt --test_output=all`

## 3. Java Testing (GUI / Frontend)
- **Framework**: JUnit 4.
- **Location**: Under `src/test/java/hibiki/`, e.g. `src/test/java/hibiki/ui/MenuBarFactoryTest.java`.
- **Anatomy of a Java Test**:
  ```java
  package hibiki.ui;

  import static org.junit.Assert.*;
  import org.junit.Before;
  import org.junit.Test;

  public class MyPanelTest {
    @Before
    public void setUp() {
      // Setup state before each test
    }

    @Test
    public void testButtonAction() {
      // Act
      ...
      // Assert
      assertTrue(condition);
    }
  }
  ```
- **Running Java Tests**:
  - Run a specific Java test target:
    `bazel test //:menu_bar_factory_test -c opt --test_output=all`
  - Run all tests in the workspace:
    `bazel test //... -c opt --test_output=all`
