/*
 * Copyright (c) 2013, 2016, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package test.launchertest;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.stage.Stage;

import static test.launchertest.Constants.*;

/**
 * Test application with no main method. This is launched by MainLauncherTest.
 */
public class TestAppThreadCheck extends Application {

    public TestAppThreadCheck() {
        if (!Platform.isFxApplicationThread()) {
            System.exit(ERROR_CONSTRUCTOR_WRONG_THREAD);
        }
    }

    @Override public void init() {
        if (Platform.isFxApplicationThread()) {
            System.exit(ERROR_INIT_WRONG_THREAD);
        }
    }

    @Override public void start(Stage stage) throws Exception {
        if (!Platform.isFxApplicationThread()) {
            System.exit(ERROR_START_WRONG_THREAD);
        }

        Platform.runLater(Platform::exit);
    }

    @Override public void stop() {
        if (!Platform.isFxApplicationThread()) {
            System.exit(ERROR_STOP_WRONG_THREAD);
        }

        System.exit(ERROR_NONE);
    }

    public static void main(String[] args) {
        if (Platform.isFxApplicationThread()) {
            System.exit(ERROR_MAIN_WRONG_THREAD);
        }

        Application.launch(args);
    }

    static {
        if (!Platform.isFxApplicationThread()) {
            System.exit(ERROR_CLASS_INIT_WRONG_THREAD);
        }

        try {
            Platform.runLater(() -> {
                // do nothing
            });
        } catch (IllegalStateException ex) {
            ex.printStackTrace();
            System.exit(ERROR_TOOLKIT_NOT_RUNNING);
        } catch (RuntimeException ex) {
            ex.printStackTrace();
            System.exit(ERROR_UNEXPECTED_EXCEPTION);
        }
    }

}
